# CMake generated Testfile for 
# Source directory: D:/wan1/C++/Libraries/ToolHead/Terminal
# Build directory: D:/wan1/C++/Libraries/ToolHead/Terminal/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(terminal_selftest "D:/wan1/C++/Libraries/ToolHead/Terminal/build/terminal_selftest.exe")
set_tests_properties(terminal_selftest PROPERTIES  _BACKTRACE_TRIPLES "D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;112;add_test;D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;0;")
add_test(archive_dll_api "D:/wan1/C++/Libraries/ToolHead/Terminal/build/archive_dll_test.exe" "D:/wan1/C++/Libraries/ToolHead/Terminal/build/TerminalArchive.dll")
set_tests_properties(archive_dll_api PROPERTIES  ENVIRONMENT "TERMINAL_ARCHIVE_EMBED=1" _BACKTRACE_TRIPLES "D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;117;add_test;D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;0;")
add_test(archive_formats_api "D:/wan1/C++/Libraries/ToolHead/Terminal/build/archive_formats_test.exe" "D:/wan1/C++/Libraries/ToolHead/Terminal/build/TerminalArchive.dll")
set_tests_properties(archive_formats_api PROPERTIES  _BACKTRACE_TRIPLES "D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;123;add_test;D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;0;")
add_test(terminal_demo_frame "D:/wan1/C++/Libraries/ToolHead/Terminal/build/terminal_demo.exe" "--frame")
set_tests_properties(terminal_demo_frame PROPERTIES  _BACKTRACE_TRIPLES "D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;126;add_test;D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;0;")
add_test(table_editor_demo_frame "D:/wan1/C++/Libraries/ToolHead/Terminal/build/table_editor_demo.exe" "--frame")
set_tests_properties(table_editor_demo_frame PROPERTIES  _BACKTRACE_TRIPLES "D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;126;add_test;D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;0;")
add_test(music_player_demo_frame "D:/wan1/C++/Libraries/ToolHead/Terminal/build/music_player_demo.exe" "--frame")
set_tests_properties(music_player_demo_frame PROPERTIES  _BACKTRACE_TRIPLES "D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;126;add_test;D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;0;")
add_test(zip_tool_demo_frame "D:/wan1/C++/Libraries/ToolHead/Terminal/build/zip_tool_demo.exe" "--frame")
set_tests_properties(zip_tool_demo_frame PROPERTIES  _BACKTRACE_TRIPLES "D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;126;add_test;D:/wan1/C++/Libraries/ToolHead/Terminal/CMakeLists.txt;0;")
