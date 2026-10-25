#include <Quickdraw.h>
#include <Lists.h>
#include <ToolUtils.h>
#include <Resources.h>
#include <string.h>
#include "Util.h"
#include "DarkListDef.h"

// Store a row of count columns of Mac Roman text. A column longer than
// kListTextMax keeps its first kListTextMax - 1 characters and an ellipsis.
void SetListRow(ListHandle list, Cell cell, const string* columns, short count)
{
	unsigned char data[1 + kListColumnsMax * (1 + kListTextMax)];
	short len = 0;

	if (count > kListColumnsMax)
		count = kListColumnsMax;
	data[len++] = (unsigned char)count;
	for (short i = 0; i < count; i++)
	{
		size_t n = columns[i].size();
		bool cut = n > (size_t)kListTextMax;
		if (cut)
			n = kListTextMax - 1;
		data[len++] = (unsigned char)(cut ? n + 1 : n);
		memcpy(data + len, columns[i].data(), n);
		len += n;
		if (cut)
			data[len++] = 0xC9;  // ... as one character in Mac Roman
	}
	LSetCell(data, len, cell, list);
}

// Indent the list's rows by width; the caller draws the list
void SetListGutter(ListHandle list, short width)
{
	(**list).refCon = ((long)width << 16) | ((**list).refCon & 0xFFFF);
}

// Put the speaker at row (-1: none). With draw, the rows that change are drawn
// at once.
void SetListMark(ListHandle list, short row, bool draw)
{
	short old = (short)((**list).refCon & 0xFFFF) - 1;
	(**list).refCon = ((**list).refCon & 0xFFFF0000) | (unsigned short)(row + 1);
	if (!draw || row == old)
		return;

	Cell cell = { 0, 0 };
	short rows = (**list).dataBounds.bottom;
	if (old >= 0 && old < rows)
	{
		cell.v = old;
		LDraw(cell, list);
	}
	if (row >= 0 && row < rows)
	{
		cell.v = row;
		LDraw(cell, list);
	}
}

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

		unsigned char data[1 + kListColumnsMax * (1 + kListTextMax)];
		short len = sizeof(data);
		LGetCell(data, &len, cell, list);

		// The gutter, and in the marked row a speaker at its left edge, level
		// with the text: 5 x 6 px, a cone widening to the right, then sound
		long refCon = (**list).refCon;
		short gutter = (short)(refCon >> 16);
		if ((short)(refCon & 0xFFFF) == cell.v + 1)
		{
			short x = cellRect->left, y = cellRect->top + 4;
			MoveTo(x, y + 2);
			MacLineTo(x, y + 3);
			MoveTo(x + 1, y + 1);
			MacLineTo(x + 1, y + 4);
			MoveTo(x + 2, y);
			MacLineTo(x + 2, y + 5);
			MoveTo(x + 4, y + 2);
			MacLineTo(x + 4, y + 3);
		}

		short count = len > 0 ? data[0] : 0;
		if (count > kListColumnsMax)
			count = kListColumnsMax;
		short width = cellRect->right - cellRect->left - gutter;
		short cellWidth = (count > 0) ? width / count : width;
		short offset = gutter;
		short pos = 1;

		for (int i = 0; i < count && pos < len && pos + 1 + data[pos] <= len; i++)
		{
			// Cut with an ellipsis where it would run into the next column
			Str255 text;
			memcpy(text, data + pos, data[pos] + 1);
			pos += data[pos] + 1;
			TruncString(cellWidth - 6, text, truncEnd);

			textMode = srcCopy;

			if (i == 0 && selected)
			{
				textMode = notSrcCopy;
			}

			TextMode(textMode);
			MoveTo(cellRect->left + offset, cellRect->top + 10);
			DrawString(text);

			offset += cellWidth;
		}

		// Restore graphics environment: the list port's first, then the port
		SetClip(savedClip);
		DisposeRgn(savedClip);
		SetPenState(&savedPenState);
		TextMode(srcCopy);
		MacSetPort(savedPort);
	}

	pascal void Dispose(ListHandle list)
	{
		// Rows are stored by value in the cells, so there is nothing to free.
		// This used to "delete &row" on a stack copy, which on a Mac hands a
		// stack address to DisposePtr and corrupts the heap; it only never
		// ran because LGetCell was given the wrong half of an int length.
	}
}
