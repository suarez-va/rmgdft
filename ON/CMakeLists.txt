list(APPEND RMGLIBS ON_Common)
list(APPEND RMGLIBS ON_Input)
list(APPEND RMGLIBS ON_NEGF_share)
list(APPEND RMGLIBS OnTddft)
list(APPEND RMGLIBS IO_Tddft)
list(APPEND RMGLIBS Eldyn)
list(APPEND RMGLIBS BerryPhase)

if(QMCPACK_SUPPORT)
    list(APPEND RMGLIBS Interfaces)
endif()
if(RMG_HIP_ENABLED)
    list(APPEND RMGLIBS Gpufuncs)
endif()
if(RMG_CUDA_ENABLED)
    list(APPEND RMGLIBS Gpufuncs)
endif()



include_directories("${PROJECT_SOURCE_DIR}/ON/Headers/")
add_executable(${rmg-on} Common/Main.cpp ${PROJECT_SOURCE_DIR}/Headers/main.h)
set_target_properties(${rmg-on} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_include_directories(${rmg-on} PRIVATE "${PROJECT_SOURCE_DIR}/ON/Headers/")


# There must be a better way to resolve circular references than this
foreach(lib ${RMGLIBS})
    target_link_libraries(${rmg-on} ${lib})
endforeach(lib ${RMGLIBS})
foreach(lib ${RMGLIBS})
    target_link_libraries(${rmg-on} ${lib})
endforeach(lib ${RMGLIBS})
foreach(lib ${RMGLIBS})
    target_link_libraries(${rmg-on} ${lib})
endforeach(lib ${RMGLIBS})
foreach(lib ${RMGLIBS})
    target_link_libraries(${rmg-on} ${lib})
endforeach(lib ${RMGLIBS})


#install(TARGETS ${rmg-on} DESTINATION bin)

