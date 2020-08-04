@echo Copying .g3a to G: (this needs to match your mounted drive letter though) 
copy oiram.g3a D:\ /Y
if errorlevel 1 pause
removedrive D: -L