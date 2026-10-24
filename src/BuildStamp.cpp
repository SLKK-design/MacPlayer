/* BuildStamp.cpp — touched before every build (see CMakeLists.txt) so
   __DATE__/__TIME__ always name the build that wrote MacPlayer Log. */
const char* BuildStamp()
{
	return __DATE__ " " __TIME__;
}
