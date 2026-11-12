@echo off
cmake -D INPUT_FILE=%1 -P %~dp0\DoxygenFileFilter.cmake 2>&1
