webengine_path=/c/Qt/Qt5.12.12.5-WebEngine-5.12.x
rm -rf $webengine_path
while read -r line; do
  for file in $line; do
    unix_file_path=`cygpath "$file"`
    prefix="/c/Qt/Qt5.12.12/5.12.12.5/msvc2015/"
    relative_file_path=${unix_file_path#"$prefix"}
    echo $relative_file_path
    if [ -f "$unix_file_path" ]; then
      install -D $unix_file_path $webengine_path/$relative_file_path
    fi
    rm -rf $unix_file_path
    # cp "$file" new_folder;
  done;
done < copy-files.txt
install -D cmake/Qt5WebEngine/Qt5WebEngineConfig.cmake $webengine_path/lib/cmake/Qt5WebEngine/Qt5WebEngineConfig.cmake
install -D cmake/Qt5WebEngineCore/Qt5WebEngineCoreConfig.cmake $webengine_path/lib/cmake/Qt5WebEngineCore/Qt5WebEngineCoreConfig.cmake
install -D cmake/Qt5WebEngineWidgets/Qt5WebEngineWidgetsConfig.cmake $webengine_path/lib/cmake/Qt5WebEngineWidgets/Qt5WebEngineWidgetsConfig.cmake
