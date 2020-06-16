#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "absl/time/time.h"
#include "absl/time/clock.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#include "cpr/cpr.h"

#include "opencensus/stats/stats.h"
#include "opencensus/exporters/stats/stackdriver/stackdriver_exporter.h"
#include "opencensus/exporters/stats/stdout/stdout_exporter.h"

#include "google/protobuf/util/json_util.h"

#include "re2/re2.h"

ABSL_FLAG( std::string, project_id, "", "stackdriver project id" );
ABSL_FLAG( std::string, instance_id, "", "local GCE instance id" );
ABSL_FLAG( std::string, zone, "", "local instance zone" );
ABSL_FLAG( bool, debug, false, "debug : print to stdout" );
ABSL_FLAG( int, period_seconds, 60, "perform a measurement every N seconds" );

// Monitored resource
// FIXME : how do I detect if I am on a gce instance, a container inside a k8s_pod in gke ? a container in gce ?
constexpr char kPodMonitoredResource[] = "k8s_pod";
constexpr char kContainerMonitoredResource[] = "k8s_container";
constexpr char kGCEInstanceMonitoredResource[] = "gce_instance";
constexpr char kProjectIDLabel[] = "project_id";
constexpr char kLocationLabel[] = "location";
constexpr char kClusterNameLabel[] = "cluster_name";
constexpr char kNamespaceNameLabel[] = "namespace_name";
constexpr char kPodNameLabel[] = "pod_name";
constexpr char kContainerNameLabel[] = "container_name";
constexpr char kGCEInstanceIDLabel[] = "instance_id";
constexpr char kZoneLabel[] = "zone";

std::string ConvertMessageToJson( google::protobuf::Message const * poMsg )
{
  std::string strMsg;

  google::protobuf::util::JsonOptions stOpt;

  stOpt.always_print_enums_as_ints = true;
  stOpt.always_print_primitive_fields = true;
  stOpt.preserve_proto_field_names = true;

  google::protobuf::util::MessageToJsonString( *poMsg, &strMsg, stOpt );

  return strMsg;
}

/*
   The purpose of this executable is to collect /proc/meminfo and /proc/vmstat 
   into stackdriver monitoring metrics, using C+, OpenCensus, CMake...
   
   FUNCTIONALITY TODO:
      - help message
      - programmatically change the metrics prefix.
      - observe GCE metadata server to dynamically stop and start exporting, or change sampling rate
      - use flag or same metadata server mechanism to decide if we actually want 130+ /proc/vmstat metrics?
      - detect which metrics are never implemented and therefore should not be exported at all
      - more complex views ( spool metrics for a while, resample/denoise and submit ? )
      - stackdriver project id different from monitoring resource project id ?
      - detect if we are running from GCE VM, container in a GCE VM, container in GKE ...
      
      - discriminate by labels? 
      - create correct local aggregates for /proc/meminfo that make sense...
      - observe specific process names ? /proc/[pid]/status, /proc[pid]/smap_rollup ? /proc/[pid]/oom_* ? 
      
      - somewhere else I did a python collectd plugin for nstat. implement as a similar opencensus program ?
      
   TODO:
      - fix all warnings.
      - clang-format like the rest of repository ?
      - build as a separate CMake project
      - build as a Bazel example
      - build as a separate Bazel project
      - review all the licenses ( re2, cpr, opencensus-cpp, protobuf and abseil )
      
   Ideally this should be implemented as a collectd (C) plugin and exporter, and widely distributed...
   
 */

int main( int argc, char** argv )
{

  // FIXME : set --help message. for now only --helpfull works.
  
  std::vector<char*> positionalArgs =  absl::ParseCommandLine( argc, argv );

  if ( absl::GetFlag( FLAGS_debug ) ) {
    
    std::cout << "DEBUG MODE: registering an stdout stats exporter only" << std::endl;
    
    opencensus::exporters::stats::StdoutExporter::Register();

  } else {
    
    std::cout << "PROD MODE: registering a stackdriver exporter only" << std::endl;

    opencensus::exporters::stats::StackdriverOptions statsOps;

    // FIXME : configurable prefix.
    statsOps.metric_name_prefix = "custom.googleapis.com/slb/zenith/";

    std::string  project_id = absl::GetFlag( FLAGS_project_id  );
    std::string instance_id = absl::GetFlag( FLAGS_instance_id );
    std::string        zone = absl::GetFlag( FLAGS_zone        );

    if ( project_id.empty() ){

        auto r = cpr::Get(cpr::Url{"http://169.254.169.254/computeMetadata/v1/project/project-id"},
                          cpr::Header{{"Metadata-Flavor", "Google"}} );
        r.status_code;                  // 200
        r.header["content-type"];       // application/json; charset=utf-8
        r.text;                         // JSON text string

        // handle retry, errors,
        if ( r.status_code == 200 ){
          project_id = r.text;
        } else {
          std::cerr << "could not obtain vm project name from metadata server" << std::endl;
          return 1;
        }
    }

    if ( instance_id.empty() ){

        auto r = cpr::Get(cpr::Url{"http://169.254.169.254/computeMetadata/v1/instance/name"},
                          cpr::Header{{"Metadata-Flavor", "Google"}} );
        r.status_code;                  // 200
        r.header["content-type"];       // application/json; charset=utf-8
        r.text;                         // JSON text string

        // handle retry, errors,
        if ( r.status_code == 200 ){
          instance_id = r.text;
        } else {
          std::cerr << "could not obtain vm instance name from metadata server" << std::endl;
          return 1;
        }
    }

    if ( zone.empty() ){

        auto r = cpr::Get(cpr::Url{"http://169.254.169.254/computeMetadata/v1/instance/zone"},
                          //cpr::Parameters{{"recursive", "true"}, {"alt", "text"}},
                          cpr::Header{{"Metadata-Flavor", "Google"}} );
        r.status_code;                  // 200
        r.header["content-type"];       // application/json; charset=utf-8
        r.text;                         // JSON text string

        // handle retry, errors,
        if ( r.status_code == 200 ){
          //  projects/REDACTED/zones/us-central1-c
          std::vector<std::string> v = absl::StrSplit(r.text, '/');
          zone = v.back();
        } else {
          std::cerr << "could not obtain vm zone from metadata server" << std::endl;
          return 1;
        }
    }

    statsOps.project_id = project_id;
    statsOps.monitored_resource.set_type( kGCEInstanceMonitoredResource );
    
    // FIXME : handle a stackdriver project id different from gce project id ?
    //statsOps.monitored_resource.mutable_labels()["project_id" ] = ;
    (*statsOps.monitored_resource.mutable_labels())[ kGCEInstanceIDLabel ] = instance_id;
    (*statsOps.monitored_resource.mutable_labels())[ kZoneLabel ] = zone;

    std::cout << "using stackdriver project_id " << statsOps.project_id << "\n";
    std::cout << "using monitored resource " << ConvertMessageToJson( &( statsOps.monitored_resource ) ) << std::endl;

    opencensus::exporters::stats::StackdriverExporter::Register( std::move( statsOps ) );
  }

  std::cout << "parsing /proc/meminfo and /proc/vmstat every " << absl::GetFlag( FLAGS_period_seconds ) << " seconds " << std::endl;

  // man proc
  std::map< std::string, std::string >  meminfoDescriptions  = {
    { "MemTotal",       "Total usable RAM (i.e., physical RAM minus a few reserved bits and the kernel binary code)." },
    { "MemFree",        "The sum of LowFree+HighFree." } ,
    { "MemAvailable",   "An estimate of how much memory is available for starting new applications, without swapping." } ,
    { "Buffers",        "Relatively temporary storage for raw disk blocks that shouldn't get tremendously large (20MB or so)." } ,
    { "Cached",         "In-memory cache for files read from the disk (the page cache).  Doesn't include SwapCached." } ,
    { "SwapCached",     "Memory that once was swapped out, is swapped back in but still also is in the swap file.  "
                        "(If memory pressure is high, these pages don't need to be swapped out again because they are already in the swap file.  This saves I/O.)" } ,
    { "Active",         "Memory that has been used more recently and usually not reclaimed unless absolutely necessary." } ,
    { "Inactive",       "Memory which has been less recently used.  It is more eligible to be reclaimed for other purposes." } ,
    { "Active(anon)",   "[To be documented.]" } ,
    { "Inactive(anon)", "[To be documented.]" } ,
    { "Active(file)",   "[To be documented.]" } ,
    { "Inactive(file)", "[To be documented.]" } ,
    { "Unevictable",    "[To be documented.]" } ,
    { "Mlocked",        "[To be documented.]" } ,
    { "HighTotal",      "Total amount of highmem.  Highmem is all memory above ~860MB of physical memory.  Highmem areas are for use by user-space programs, or for the page cache.  "
                        "The kernel must use tricks to access this memory, making it slower to access than lowmem." } ,
    { "HighFree",       "Amount of free highmem." } ,
    { "LowTotal",       "Total amount of lowmem.  Lowmem is memory which can be used for everything that highmem can be used for, but it is also available for the kernel's use for its own data structures.  "
                        "Among many other things, it  is  where  everything from Slab is allocated.  Bad things happen when you're out of lowmem." } ,
    { "LowFree",        "Amount of free lowmem." } ,
    { "MmapCopy",       "[To be documented.]" } ,
    { "SwapTotal",      "Total amount of swap space available." } ,
    { "SwapFree",       "Amount of swap space that is currently unused." } ,
    { "Dirty",          "Memory which is waiting to get written back to the disk." } ,
    { "Writeback",      "Memory which is actively being written back to the disk." } ,
    { "AnonPages",      "Non-file backed pages mapped into user-space page tables." } ,
    { "Mapped",         "Files which have been mapped into memory (with mmap(2)), such as libraries." } ,
    { "Shmem",          "Amount of memory consumed in tmpfs(5) filesystems." } ,
    { "Slab",           "In-kernel data structures cache.  (See slabinfo(5).)" } ,
    { "SReclaimable",   "Part of Slab, that might be reclaimed, such as caches." } ,
    { "SUnreclaim",     "Part of Slab, that cannot be reclaimed on memory pressure." } ,
    { "KernelStack",    "Amount of memory allocated to kernel stacks." } ,
    { "PageTables",     "Amount of memory dedicated to the lowest level of page tables." } ,
    { "Quicklists",     "[To be documented.]" } ,
    { "NFS_Unstable",   "NFS pages sent to the server, but not yet committed to stable storage." } ,
    { "Bounce",         "Memory used for block device \"bounce buffers\"." } ,
    { "WritebackTmp",   "Memory used by FUSE for temporary writeback buffers." } ,
    { "CommitLimit",    "This  is  the  total amount of memory currently available to be allocated on the system, expressed in kilobytes.  "
                        "This limit is adhered to only if strict overcommit accounting is enabled (mode 2 in /proc/sys/vm/overcommit_memory).  "
                        "The limit is calculated according to the formula described under /proc/sys/vm/overcommit_memory. "
                        "For further details, see the kernel source file Documentation/vm/overcommit-accounting."
    } ,
    { "Committed_AS",   "The amount of memory presently allocated on the system.  The committed memory is a sum of all of the memory which has been allocated by processes,"
                        " even if it has not been \"used\" by them as of yet.  A process which allocates 1GB of memory (using malloc(3) or similar), but touches only 300MB"
                        " of that memory will show up as using only 300MB of memory even if it has the address space allocated for the entire 1GB."
                        "\n"
                        "This  1GB  is  memory  which has been \"committed\" to by the VM and can be used at any time by the allocating application.  "
                        "With strict overcommit enabled on the system (mode 2 in /proc/sys/vm/overcommit_memory), allocations which would exceed the CommitLimit will not be permitted.  "
                        "This is useful if one needs to guarantee that processes will not fail due to lack of memory once that memory has been successfully allocated."
    } ,
    { "VmallocTotal",   "Total size of vmalloc memory area." } ,
    { "VmallocUsed",    "Amount of vmalloc area which is used." } ,
    { "VmallocChunk",   "Largest contiguous block of vmalloc area which is free." } ,
    { "HardwareCorrupted", "[To be documented.]" } ,
    { "AnonHugePages",  "Non-file backed huge pages mapped into user-space page tables." } ,
    { "ShmemHugePages", "Memory used by shared memory (shmem) and tmpfs(5) allocated with huge pages" } ,
    { "ShmemPmdMapped", "Shared memory mapped into user space with huge pages." } ,
    { "CmaTotal",       "Total CMA (Contiguous Memory Allocator) pages." } ,
    { "CmaFree",        "Free CMA (Contiguous Memory Allocator) pages." } ,
    { "HugePages_Total", "The size of the pool of huge pages." } ,
    { "HugePages_Free", "The number of huge pages in the pool that are not yet allocated." } ,
    { "HugePages_Rsvd", "This is the number of huge pages for which a commitment to allocate from the pool has been made, but no allocation has yet been made.  "
                        "These reserved huge pages guarantee that an application will be able to allocate a huge page  from  the  pool  of  huge "
                        "pages at fault time."
    } ,
    { "HugePages_Surp", "This is the number of huge pages in the pool above the value in /proc/sys/vm/nr_hugepages. "
                        "The maximum number of surplus huge pages is controlled by /proc/sys/vm/nr_overcommit_hugepages."
    } ,
    { "Hugepagesize",   "The size of huge pages." } ,
    { "DirectMap4k",    "Number of bytes of RAM linearly mapped by kernel in 4kB pages." } ,
    { "DirectMap4M",    "Number of bytes of RAM linearly mapped by kernel in 4MB pages." } ,
    { "DirectMap2M",    "Number of bytes of RAM linearly mapped by kernel in 2MB pages." } ,
    { "DirectMap1G",    "Number of bytes of RAM linearly mapped by kernel in 1GB pages." }
  };

  // is there _any_ documentation for /proc/vmstat _anywhere ?
  
  bool firstTime = true;

  std::map< std::string, std::int64_t > meminfoByteValues, meminfoCountValues, vmstatCountValues;
  std::map< std::string, std::pair< opencensus::stats::MeasureInt64, std::int64_t > > meminfoByteMeasures, meminfoCountMeasures, vmstatCountMeasures;

  re2::RE2 static const countRe( "(.+):\\s+([[:alnum:]]+)" );
  re2::RE2 static const byteRe ( "(.+):\\s+([[:alnum:]]+) kB");
  re2::RE2 static const vmstatRe( "(.+)\\s+([[:alnum:]]+)" );

  while ( true ){

    std::ifstream ifile( "/proc/meminfo" );

    for ( std::string line; std::getline( ifile, line ); ){

      std::string label;
      std::int64_t count;

      if        ( re2::RE2::FullMatch( line,  byteRe, &label, &count ) ){
         meminfoByteValues[label] = count * 1024 ;
      } else if ( re2::RE2::FullMatch( line, countRe, &label, &count ) ){
        meminfoCountValues[label] = count;
      }
    }

    ifile.close();

    ifile.open( "/proc/vmstat");

    for ( std::string line; std::getline( ifile, line ); ){

      std::string label;
      std::int64_t count;

      if ( re2::RE2::FullMatch( line, vmstatRe, &label, &count ) ){
        vmstatCountValues[label] = count;
      }
    }

    ifile.close();

    // FIXME : hardly elegant.
    if ( firstTime ){

      for ( auto const & p : meminfoByteValues ){
        
        // FIXME : metric vs measure name ?
        
        // register a measure
        auto mn = absl::StrCat( "proc/meminfo/", p.first );
        opencensus::stats::MeasureInt64 m( opencensus::stats::MeasureInt64::Register( mn, p.first, "bytes" ) );

        // register a view
        auto vn = absl::StrCat( "proc/meminfo/", p.first , "_view" ); // do I want the _view suffix ?
        auto desc = ( meminfoDescriptions.count( p.first ) > 0 ) ? meminfoDescriptions.at( p.first ) : absl::StrCat( p.first, " in bytes as per /proc/meminfo" );
        auto vd = opencensus::stats::ViewDescriptor()
          .set_name( vn )
          .set_measure( mn )
          .set_aggregation( opencensus::stats::Aggregation::LastValue() ) //is this correct ?
          .set_description( desc ); 

        vd.RegisterForExport();
        meminfoByteMeasures.insert( { p.first,  { m,  meminfoByteValues.at(p.first) } } );
      }

      for ( auto const & p : meminfoCountValues ){
        auto mn = absl::StrCat( "proc/meminfo/", p.first );
        opencensus::stats::MeasureInt64 m( opencensus::stats::MeasureInt64::Register( mn, p.first, "") );

        auto vn = absl::StrCat( "proc/meminfo/", p.first , "_view" ); // do I want the view suffix ?
        auto desc = ( meminfoDescriptions.count( p.first ) > 0 ) ? meminfoDescriptions.at( p.first ) : absl::StrCat( p.first, " count as per /proc/meminfo" );
        auto vd = opencensus::stats::ViewDescriptor()
          .set_name( vn )
          .set_measure( mn )
          .set_aggregation( opencensus::stats::Aggregation::LastValue())
          .set_description( desc );

        vd.RegisterForExport();

        meminfoCountMeasures.insert( { p.first,  { m, meminfoCountValues.at(p.first) } } );
      }

      for ( auto const & p : vmstatCountValues ){
        auto mn = absl::StrCat( "proc/vmstat/", p.first );
        opencensus::stats::MeasureInt64 m( opencensus::stats::MeasureInt64::Register( mn, p.first, "") );

        auto vn = absl::StrCat( "proc/vmstat/", p.first );
        auto desc = absl::StrCat( p.first, " count as per /proc/vmstat" );
        auto vd = opencensus::stats::ViewDescriptor()
          .set_name( vn )
          .set_measure( mn )
          .set_aggregation( opencensus::stats::Aggregation::LastValue())
          .set_description( desc );

        vd.RegisterForExport();

        vmstatCountMeasures.insert( { p.first,  { m, vmstatCountValues.at(p.first) } } );
      }


      firstTime = false;
    }

    for ( auto & p : meminfoByteMeasures ){
      opencensus::stats::Record( {{ p.second.first, meminfoByteValues.at(p.first) }} );
    }

    for ( auto  & p : meminfoCountMeasures ){
      opencensus::stats::Record( {{ p.second.first, meminfoCountValues.at(p.first) }} );
    }

    for ( auto  & p : vmstatCountMeasures ){
      opencensus::stats::Record( {{ p.second.first, vmstatCountValues.at(p.first) }} );
    }

    absl::SleepFor( absl::Seconds( absl::GetFlag( FLAGS_period_seconds  ) ) );
    
  } // infinite loop

  return 0;
}
