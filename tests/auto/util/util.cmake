if (NOT TARGET Test::Util)
   add_library(qtestutil INTERFACE)
   target_include_directories(qtestutil INTERFACE ${CMAKE_CURRENT_LIST_DIR})
   add_library(Test::Util ALIAS qtestutil)
endif()
