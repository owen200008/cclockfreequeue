cmake_minimum_required(VERSION 2.8.12)

project(lockfreequeue)

set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_BUGOR 0)

set(BasicLibSrcPath ${PROJECT_SOURCE_DIR})

macro(source_group_by_dir source_files)
    if(MSVC)
        set(sgbd_cur_dir ${PROJECT_SOURCE_DIR})
        foreach(sgbd_file ${${source_files}})
			set(samefilename FALSE)
			while (NOT samefilename)
				string(REGEX REPLACE "/[^/\\.]*/\\.\\./" "/" sgbd_fpath ${sgbd_file})
				string(COMPARE EQUAL ${sgbd_fpath} ${sgbd_file} samefilename)
				set(sgbd_file ${sgbd_fpath})
			endwhile()
            string(REGEX REPLACE "${sgbd_cur_dir}/" "" sgbd_fpath ${sgbd_file})
			string(REGEX REPLACE "(.*)/" "" sgbd_group_name ${sgbd_fpath})
			string(COMPARE EQUAL ${sgbd_fpath} ${sgbd_group_name} sgbd_nogroup)
			if(sgbd_nogroup)
				set(sgbd_real_group_name "\\")
			else()
				string(REGEX REPLACE "/${sgbd_group_name}" "" sgbd_real_group_name ${sgbd_fpath})
				string(REPLACE "/" "\\" sgbd_real_group_name ${sgbd_real_group_name})
			endif()
			source_group(${sgbd_real_group_name} FILES ${sgbd_file})
        endforeach(sgbd_file)
    endif(MSVC)
endmacro(source_group_by_dir)

#fourth self build
#extra src
set(BasicLibExtra_Src)
#flags
set(BasicLibCMakeCXX_FLAGS ${CMAKE_CXX_FLAGS})
set(BasicLibCMakeC_FLAGS ${CMAKE_C_FLAGS})
#define
set(BasicLibDefinitions)
#link add event
set(BasicLibLinks)

if(MSVC)
	set(BasicLibCMakeCXX_FLAGS "${BasicLibCMakeCXX_FLAGS} /MP")
	set(BasicLibCMakeC_FLAGS "${BasicLibCMakeC_FLAGS} /MP")
	set(BasicLibDefinitions ${BasicLibDefinitions} -DUNICODE -D_UNICODE)
else()
	set(BasicLibCMakeCXX_FLAGS "${BasicLibCMakeCXX_FLAGS} --std=c++11 -frtti -fexceptions")
	set(BasicLibLinks ${BasicLibLinks} pthread)
endif()

#define
add_definitions(${BasicLibDefinitions})
#flags
set(CMAKE_CXX_FLAGS ${BasicLibCMakeCXX_FLAGS})
set(CMAKE_C_FLAGS ${BasicLibCMakeC_FLAGS})

#src
set(basiclib_SOURCES 
	${BasicLibSrcPath}/cccontaintemplate.h
    ${BasicLibSrcPath}/cclockfreedef.h
	${BasicLibSrcPath}/cclockfreequeue.h
    ${BasicLibSrcPath}/cclockfreefixqueue.h
	${BasicLibSrcPath}/debug.cpp
	${BasicLibSrcPath}/debug.h
	${BasicLibSrcPath}/headdefine.h
	${BasicLibSrcPath}/lockfreemessagequeue.h
	${BasicLibSrcPath}/main.cpp
	)
set(TotalSourceData ${basiclib_SOURCES} ${BasicLibExtra_Src})
source_group_by_dir(TotalSourceData)

add_executable(lockfreequeue ${basiclib_SOURCES} ${BasicLibExtra_Src})
target_link_libraries(lockfreequeue ${BasicLibLinks})











