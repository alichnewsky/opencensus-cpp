opencensus_lib( exporters_stats_stackdriver
                PUBLIC
                SRCS
                internal/stackdriver_exporter.cc
                DEPS
                common_hostname
                common_grpc_status
                common_grpc_with_user_agent
                stats
                exporters_stats_stackdriver_utils
)

opencensus_lib( exporters_stats_stackdriver_utils
                PUBLIC
                SRCS
                internal/stackdriver_utils.cc
                DEPS
                stats
		common_timestamp
)

opencensus_lib( time_series_matcher
                PUBLIC
                SRCS
                internal/time_series_matcher.cc
                DEPS
                stats
)

# why can't I just declare googleapis-c++::foo_bar ? did I fuck up my exporting ?


find_package( googleapis CONFIG REQUIRED googleapis_cpp_monitoring_v3_protos )

# this works
get_target_property( _iface_incdir googleapis_cpp_monitoring_v3_protos INTERFACE_INCLUDE_DIRECTORIES )
target_include_directories(  opencensus_exporters_stats_stackdriver      PRIVATE ${_iface_incdir}  )
target_include_directories(  opencensus_exporters_stats_stackdriver_utils PRIVATE ${_iface_incdir}  )

#target_include_directories(  opencensus_exporters_stats_stackdriver       PRIVATE googleapis_cpp_monitoring_v3_protos )
#target_include_directories(  opencensus_exporters_stats_stackdriver_utils PRIVATE googleapis_cpp_monitoring_v3_protos )
target_link_libraries( opencensus_exporters_stats_stackdriver       PUBLIC googleapis_cpp_monitoring_v3_protos )
target_link_libraries( opencensus_exporters_stats_stackdriver_utils PUBLIC googleapis_cpp_monitoring_v3_protos )

target_include_directories( opencensus_time_series_matcher PUBLIC gmock googleapis_cpp_monitoring_v3_protos )
target_link_libraries     ( opencensus_time_series_matcher PUBLIC gmock googleapis_cpp_monitoring_v3_protos )

opencensus_test(exporters_stats_stackdriver_utils_test
                internal/stackdriver_utils_test.cc
                exporters_stats_stackdriver_utils
                time_series_matcher
                stats
                stats_test_utils
)

opencensus_test(exporters_stats_stackdriver_e2e_test
                internal/stackdriver_e2e_test.cc
                exporters_stats_stackdriver
                exporters_stats_stackdriver_utils
                time_series_matcher
                stats
)
