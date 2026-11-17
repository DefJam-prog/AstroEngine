if(NOT DEFINED APP OR NOT EXISTS "${APP}")
    message(FATAL_ERROR "3D Editor benchmark executable is missing: ${APP}")
endif()

execute_process(
    COMMAND "${APP}" --benchmark-3d
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 150)

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "3D Editor benchmark failed (${_result})\n${_stdout}\n${_stderr}")
endif()

foreach(_mesh_count IN ITEMS 1 100 500 1000)
    if(NOT _stdout MATCHES "\\[editor-3d-frame-benchmark\\] scene=mesh-grid-${_mesh_count} meshes=${_mesh_count} entities=[0-9]+ scene_target=[0-9]+x[0-9]+ measured_frames=48")
        message(FATAL_ERROR "3D Editor benchmark omitted the ${_mesh_count}-mesh scenario or full frame sample set\n${_stdout}\n${_stderr}")
    endif()
endforeach()

if(NOT _stdout MATCHES "mesh_prepare_p50_ms=[0-9.]+ mesh_prepare_p95_ms=[0-9.]+ mesh_submit_p50_ms=[0-9.]+ mesh_submit_p95_ms=[0-9.]+ scene3d_cpu_p50_ms=[0-9.]+ scene3d_cpu_p95_ms=[0-9.]+ app_frame_wall_p50_ms=[0-9.]+ app_frame_wall_p95_ms=[0-9.]+")
    message(FATAL_ERROR "3D Editor benchmark omitted CPU preparation/submission or complete frame timing\n${_stdout}\n${_stderr}")
endif()

if(NOT _stdout MATCHES "mesh_asset_import_ms=[0-9.]+ scene_build_ms=[0-9.]+ process_working_set_mb=([0-9.]+|unavailable) process_private_mb=([0-9.]+|unavailable) editor_executable_bytes=([0-9]+|unavailable) draw_calls_p50=[0-9.]+ mesh_draws_last_frame=1000")
    message(FATAL_ERROR "3D Editor benchmark omitted fixture load, memory, executable size, or final draw-count metrics\n${_stdout}\n${_stderr}")
endif()

message(STATUS "3D Editor full-frame benchmark smoke passed; 4 deterministic mesh counts produced 48 samples each")
