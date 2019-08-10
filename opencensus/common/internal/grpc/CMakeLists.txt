find_package( protobuf CONFIG REQUIRED)  # for now, I need that and protobuf_MODULE_COMPATIBLE=ON
find_package( gRPC CONFIG REQUIRED) 

opencensus_lib(common_grpc_status
	      SRCS
	      status.cc
	      DEPS
	      absl::strings
)
target_include_directories( opencensus_common_grpc_status PUBLIC gRPC::grpc++ )
target_link_libraries     ( opencensus_common_grpc_status PUBLIC gRPC::grpc++ )

opencensus_test(common_grpc_status_test
		status_test.cc
		common_grpc_status )

opencensus_lib(common_grpc_with_user_agent
	      SRCS
	      with_user_agent.cc
	      DEPS
	      absl::strings
)
target_include_directories( opencensus_common_grpc_with_user_agent PUBLIC gRPC::grpc++ )
target_link_libraries     ( opencensus_common_grpc_with_user_agent PUBLIC gRPC::grpc++ )
