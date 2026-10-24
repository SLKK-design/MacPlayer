#ifndef __URI__
#define __URI__

#include <string>

using namespace std;

class Uri
{
public:
	Uri();
	Uri(string uriStr);
	bool Valid;
	string Scheme;
	string Host;
	string Path;
	static bool IsAbsolute(const string& uriStr);
	static string Encode(const string &value); /* MacPlayer: percent-encode a query value */
};

#endif