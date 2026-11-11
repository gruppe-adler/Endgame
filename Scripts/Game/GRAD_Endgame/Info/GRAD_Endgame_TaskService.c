//------------------------------------------------------------------------------------------------
//! Endgame objectives as native Reforger tasks (SCR_TaskSystem), replacing the local map markers
//! (decided 2026-11-11). Tasks show in the vanilla task list and on the map, survive join in
//! progress on their own (the task system replicates its data), and support progress bars.
//!
//! Every Endgame task belongs to ONE side: ownership and visibility FACTION, plus that faction key.
//! Our ids ("doc_3", "radio_0", "cipher", ...) become task ids "grad_eg_<faction>_<id>", so the
//! same objective can exist once per side.
//!
//! Server only. Rules from the BI wiki (Task System Usage): manage tasks only through
//! SCR_TaskSystem, read them through SCR_Task getters, never touch SCR_TaskData.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-11): not compiled, not run. Enum value names
//! (SCR_ETaskOwnership.FACTION, SCR_ETaskVisibility.FACTION, SCR_ETaskUIVisibility.ALL / LIST_ONLY,
//! SCR_ETaskState.COMPLETED / FAILED / CANCELLED) are taken from the wiki's descriptions and need a
//! compile to confirm; they are all used in this file only.
class GRAD_Endgame_TaskService
{
	//! Vanilla task prefab with progress and child tasks (SCR_TaskSystem.EXTENDED_TASK_RESOURCE).
	static const ResourceName TASK_PREFAB = "{CF6A2A13DF7B1018}Prefabs/Tasks/ExtendedTask.et";
	static const string PREFIX = "grad_eg_";

	//! task ids this service created, to finish or remove them at the end of a round
	protected static ref set<string> s_aOwnTaskIds = new set<string>();

	//------------------------------------------------------------------------------------------------
	static string MakeTaskId(string factionKey, string id)
	{
		return PREFIX + factionKey + "_" + id;
	}

	//------------------------------------------------------------------------------------------------
	static SCR_Task Find(string factionKey, string id)
	{
		return SCR_TaskSystem.GetTaskFromTaskID(MakeTaskId(factionKey, id), false);
	}

	//------------------------------------------------------------------------------------------------
	//! Create the task, or move and rename it if it exists. Returns the task or null.
	//! \param[in] parentId optional: our id of a parent task of the same side (one level only)
	static SCR_Task Upsert(string factionKey, string id, string name, string description, vector position, string parentId = "")
	{
		if (!Replication.IsServer() || factionKey.IsEmpty())
			return null;

		SCR_TaskSystem taskSystem = SCR_TaskSystem.GetInstance();
		if (!taskSystem)
		{
			GRAD_Endgame_MatchLog.Error("SCR_TaskSystem is missing; objectives cannot be shown.");
			return null;
		}

		SCR_Task task = Find(factionKey, id);
		if (task)
		{
			taskSystem.MoveTask(task, position);
			return task;
		}

		string taskId = MakeTaskId(factionKey, id);
		task = taskSystem.CreateTask(TASK_PREFAB, taskId, name, description, position);
		if (!task)
		{
			GRAD_Endgame_MatchLog.Error("Task could not be created: " + taskId);
			return null;
		}

		// Only this side owns and sees it.
		taskSystem.SetTaskOwnership(task, SCR_ETaskOwnership.FACTION);
		taskSystem.SetTaskVisibility(task, SCR_ETaskVisibility.FACTION);
		// A task without a place (the "Secure the documents" parent) belongs in the list only.
		if (position == vector.Zero)
			taskSystem.SetTaskUIVisibility(task, SCR_ETaskUIVisibility.LIST_ONLY);
		else
			taskSystem.SetTaskUIVisibility(task, SCR_ETaskUIVisibility.ALL);
		taskSystem.AddTaskFaction(task, factionKey);

		if (!parentId.IsEmpty())
		{
			SCR_Task parent = Find(factionKey, parentId);
			if (parent)
				taskSystem.AddChildTaskTo(parent, task);
		}

		s_aOwnTaskIds.Insert(taskId);
		return task;
	}

	//------------------------------------------------------------------------------------------------
	//! Show a progress bar on the task and set it, 0..1. Values are rounded to whole percent and
	//! only sent when they change, because the whole task replicates as one package.
	static void SetProgress(string factionKey, string id, float fraction)
	{
		SCR_Task task = Find(factionKey, id);
		SCR_TaskSystem taskSystem = SCR_TaskSystem.GetInstance();
		if (!task || !taskSystem)
			return;

		float percent = Math.Round(Math.Clamp(fraction, 0, 1) * 100);
		if (taskSystem.GetTaskProgress(task) == percent)
			return;

		taskSystem.ShowProgressForTask(task, true);
		taskSystem.SetTaskProgress(task, percent);
	}

	//------------------------------------------------------------------------------------------------
	static void Complete(string factionKey, string id)
	{
		SetState(factionKey, id, SCR_ETaskState.COMPLETED);
	}

	//------------------------------------------------------------------------------------------------
	static void Fail(string factionKey, string id)
	{
		SetState(factionKey, id, SCR_ETaskState.FAILED);
	}

	//------------------------------------------------------------------------------------------------
	protected static void SetState(string factionKey, string id, SCR_ETaskState state)
	{
		SCR_Task task = Find(factionKey, id);
		SCR_TaskSystem taskSystem = SCR_TaskSystem.GetInstance();
		if (task && taskSystem)
			taskSystem.SetTaskState(task, state);
	}

	//------------------------------------------------------------------------------------------------
	//! Remove the task without a result (objective no longer relevant, e.g. a fix that expired).
	static void Remove(string factionKey, string id)
	{
		SCR_Task task = Find(factionKey, id);
		SCR_TaskSystem taskSystem = SCR_TaskSystem.GetInstance();
		if (!task || !taskSystem)
			return;

		s_aOwnTaskIds.RemoveItem(MakeTaskId(factionKey, id));
		taskSystem.DeleteTask(task);
	}

	//------------------------------------------------------------------------------------------------
	//! Match over: every open task of the winner is completed, of the loser failed. A draw fails
	//! everything still open.
	static void FinishAll(string winnerKey)
	{
		SCR_TaskSystem taskSystem = SCR_TaskSystem.GetInstance();
		if (!Replication.IsServer() || !taskSystem)
			return;

		foreach (string taskId : s_aOwnTaskIds)
		{
			SCR_Task task = SCR_TaskSystem.GetTaskFromTaskID(taskId, false);
			if (!task)
				continue;

			SCR_ETaskState state = taskSystem.GetTaskState(task);
			if (state == SCR_ETaskState.COMPLETED || state == SCR_ETaskState.FAILED)
				continue;

			if (!winnerKey.IsEmpty() && taskId.StartsWith(PREFIX + winnerKey + "_"))
				taskSystem.SetTaskState(task, SCR_ETaskState.COMPLETED);
			else
				taskSystem.SetTaskState(task, SCR_ETaskState.FAILED);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! New round: delete every task this service made.
	static void Reset()
	{
		SCR_TaskSystem taskSystem = SCR_TaskSystem.GetInstance();
		if (!Replication.IsServer() || !taskSystem)
		{
			s_aOwnTaskIds.Clear();
			return;
		}

		foreach (string taskId : s_aOwnTaskIds)
		{
			SCR_Task task = SCR_TaskSystem.GetTaskFromTaskID(taskId, false);
			if (task)
				taskSystem.DeleteTask(task);
		}
		s_aOwnTaskIds.Clear();
	}
}
