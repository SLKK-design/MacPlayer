#include <Quickdraw.h>
#include <Lists.h>
#include <ToolUtils.h>
#include <Resources.h>
#include <string.h>
#include "Util.h"
#include "DarkListDef.h"

extern "C"
{

	extern pascal long DarkScrollbarDef(short varCode, ControlHandle theControl, short message, long param);


	pascal void DarkListDef(short message, Boolean selected, Rect* cellRect, Cell cell, short dataOffset, short dataLen, ListHandle list)
	{
		switch (message)
		{
			case lDrawMsg:
			case lHiliteMsg:
				DrawCell(selected, cellRect, cell, dataOffset, dataLen, list);
				break;
			case lCloseMsg:
				Dispose(list);
				break;
		}
	}

	pascal void DrawCell(Boolean selected, Rect* cellRect, Cell cell, short dataOffset, short dataLen, ListHandle list)
	{
		short textMode;

		// Snapshot drawing environment
		GrafPtr savedPort;
		RgnHandle savedClip;
		PenState savedPenState;

		GetPort(&savedPort); 
		MacSetPort((**list).port);
		savedClip = NewRgn();
		GetClip(savedClip);
		ClipRect(cellRect);
		GetPenState(&savedPenState);

		PenNormal();
		EraseRect(cellRect);

		TextFont(1);
		TextSize(9);
		ForeColor(whiteColor);

		ListRow row;
		LGetCell(&row, (short*)&dataLen, cell, list);

		short cellWidth = (row.CellCount > 0)
		    ? (cellRect->right - cellRect->left) / row.CellCount
		    : (cellRect->right - cellRect->left);
		short offset = 0;

		for (int i = 0; i < row.CellCount; i++)
		{
			textMode = srcCopy;

			if (i == 0 && selected)
			{
				textMode = notSrcCopy;
			}

			TextMode(textMode);
			MoveTo(cellRect->left + offset, cellRect->top + 10);
			DrawString((ConstStr255Param)row.Cells[i].Content);

			offset += cellWidth;
		}

		// Restore graphics environment
		MacSetPort(savedPort);
		SetClip(savedClip);
		DisposeRgn(savedClip);
		SetPenState(&savedPenState); 
		TextMode(srcCopy);
	}

	pascal void Dispose(ListHandle list)
	{
		// Rows are stored by value in the cells, so there is nothing to free.
		// This used to "delete &row" on a stack copy, which on a Mac hands a
		// stack address to DisposePtr and corrupts the heap; it only never
		// ran because LGetCell was given the wrong half of an int length.
	}
}
