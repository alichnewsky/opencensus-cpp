ARG BASE_IMAGE=google-cloud-cpp:anthony-dev
ARG NCPU=4

FROM ${BASE_IMAGE}
WORKDIR /var/tmp/build
COPY . /var/tmp/build/opencensus-cpp
WORKDIR /var/tmp/build/opencensus-cpp


# abseil is built as a third party dep. if I wasn to biuld sharedelibs, I need to pass -fPIC around
RUN  cmake -H. -Bcmake-out \
     -Dprotobuf_MODULE_COMPATIBLE:BOOL=ON \
     -DBUILD_SHARED_LIBS=YES \
     -DCMAKE_POSITION_INDEPENDENT_CODE=YES \
     -DCMAKE_BUILD_TYPE=Release 

RUN cmake --build cmake-out --target install -- -j 32
RUN ldconfig

