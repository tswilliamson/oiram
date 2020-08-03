@echo Copying .g3a to G: (this needs to match your mounted drive letter though) 
copy nesizm.g3a G:\ /Y
if errorlevel 1 pause
removedrive G: -L