// shamelessly inspired by Overthrow Mod

[BaseContainerProps()]
class GRAD_Endgame_MapMarkerLayer: SCR_MapModuleBase
{		
	protected Widget m_Widget;
	protected CanvasWidget m_Canvas;
	protected ref array<ref CanvasWidgetCommand> m_Commands;
	
	protected ResourceName m_Layout = "{A6A79ABB08D490BE}UI/Layouts/Map/MapCanvasLayer.layout";
	
	void Draw()
	{		
		
	}
	
	override void Update(float timeSlice)
	{	
		Draw();	
		if(m_Commands.Count() > 0)
		{						
			m_Canvas.SetDrawCommands(m_Commands);			
		}
	}
	
	void DrawCircle(vector center, float range, int color, int n = 36)	
	{
		PolygonDrawCommand cmd = new PolygonDrawCommand();		
		cmd.m_iColor = color;
		
		cmd.m_Vertices = new array<float>;
		
		float xcp, ycp;
		
		m_MapEntity.WorldToScreen(center[0], center[2], xcp, ycp, true);
		float r = range * m_MapEntity.GetCurrentZoom();
		
		for(int i = 0; i < n; i++)
		{
			float theta = i*(2*Math.PI/n);
			float x = xcp + r*Math.Cos(theta);
			float y = ycp + r*Math.Sin(theta);
			cmd.m_Vertices.Insert(x);
			cmd.m_Vertices.Insert(y);
		}
		
		m_Commands.Insert(cmd);
	}
	
	void DrawImage(vector center, int width, int height, SharedItemRef tex)
	{
		ImageDrawCommand cmd = new ImageDrawCommand();
		
		float xcp, ycp;		
		m_MapEntity.WorldToScreen(center[0], center[2], xcp, ycp, true);
		
		cmd.m_Position = Vector(xcp - (width/2), ycp - (height/2), 0);
		cmd.m_pTexture = tex;
		cmd.m_Size = Vector(width, height, 0);
		
		m_Commands.Insert(cmd);
	}
	
	void DrawLine(vector startPos, vector endPos, float width, int color)
	{
		PolygonDrawCommand cmd = new PolygonDrawCommand();
		cmd.m_iColor = color;
		
		cmd.m_Vertices = new array<float>;
		
		float x1, y1, x2, y2;
		m_MapEntity.WorldToScreen(startPos[0], startPos[2], x1, y1, true);
		m_MapEntity.WorldToScreen(endPos[0], endPos[2], x2, y2, true);
		
		// Create a thick line by drawing a rectangle
		float dx = x2 - x1;
		float dy = y2 - y1;
		float length = Math.Sqrt(dx * dx + dy * dy);
		
		if (length > 0)
		{
			// Perpendicular offset for width
			float offsetX = (-dy / length) * (width / 2.0);
			float offsetY = (dx / length) * (width / 2.0);
			
			// Four corners of the rectangle
			cmd.m_Vertices.Insert(x1 + offsetX);
			cmd.m_Vertices.Insert(y1 + offsetY);
			
			cmd.m_Vertices.Insert(x2 + offsetX);
			cmd.m_Vertices.Insert(y2 + offsetY);
			
			cmd.m_Vertices.Insert(x2 - offsetX);
			cmd.m_Vertices.Insert(y2 - offsetY);
			
			cmd.m_Vertices.Insert(x1 - offsetX);
			cmd.m_Vertices.Insert(y1 - offsetY);
		}
		
		m_Commands.Insert(cmd);
	}
	
	override void OnMapOpen(MapConfiguration config)
	{
		// Print("GRAD GRAD_Endgame_MapMarkerLayer: OnMapOpen called", LogLevel.NORMAL);
		
		super.OnMapOpen(config);
		
		m_Commands = new array<ref CanvasWidgetCommand>();
		
		m_Widget = GetGame().GetWorkspace().CreateWidgets(m_Layout);
		m_Widget.SetFlags(WidgetFlags.NOFOCUS | WidgetFlags.IGNORE_CURSOR);

		m_Canvas = CanvasWidget.Cast(m_Widget.FindAnyWidget("Canvas"));	
	}
	
	override void OnMapClose(MapConfiguration config)
	{
		super.OnMapClose(config);
		
		m_Widget.RemoveFromHierarchy();

	}
}