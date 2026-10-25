#include <Lists.h>
#include <string>
#include <vector>

using namespace std;

/* A list row as its cell stores it: the number of columns, then each
   column's text as a Pascal string, back to back, at its actual length.
   The List Manager keeps all cell data in at most 32 KB; rows of a fixed
   2 x 256 bytes allowed only 63 of them. */
const short kListColumnsMax = 2;  /* nav = 1, tracks = 2 */
const short kListTextMax = 48;    /* more than a column shows (about 37) */

void SetListRow(ListHandle list, Cell cell, const string* columns, short count);

extern "C"
{
	pascal void DarkListDef(short message, Boolean selected, Rect* cellRect, Cell cell, short dataOffset, short dataLen, ListHandle list);
	pascal void DrawCell(Boolean selected, Rect* cellRect, Cell cell, short dataOffset, short dataLen, ListHandle list);
	pascal void Dispose(ListHandle list);
}
