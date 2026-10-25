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

		short count = len > 0 ? data[0] : 0;
		if (count > kListColumnsMax)
			count = kListColumnsMax;
		short cellWidth = (count > 0)
		    ? (cellRect->right - cellRect->left) / count
		    : (cellRect->right - cellRect->left);
		short offset = 0;
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
