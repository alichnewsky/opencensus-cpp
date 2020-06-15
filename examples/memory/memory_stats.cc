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

#include "opencensus/stats/stats.h"
#include "opencensus/exporters/stats/stackdriver/stackdriver_exporter.h"
#include "opencensus/exporters/stats/stdout/stdout_exporter.h"

// discover the monitored resource for this VM !?
// see https://github.com/istio/proxyproxy/extensions/stackdriver/metric/registry.cc
// for another c++ based project, one that uses stackdriver directly though.
// istio, envoy are good examples to look at...

#include "re2/re2.h"


// obtain default from metadata server ?
ABSL_FLAG( std::string, project_id, "", "stackdriver project id" );
ABSL_FLAG( std::string, instance_id, "", "local GCE instance id" );
ABSL_FLAG( std::string, zone, "", "local instance zone" );
ABSL_FLAG( bool, debug, false, "debug : print to stdout" );
ABSL_FLAG( int, period_seconds, 60, "perform a measurement every N seconds" );


// read istio/proxy
// Monitored resource
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

int main( int argc, char** argv )
{

  std::vector<char*> positionalArgs =  absl::ParseCommandLine( argc, argv );


  if ( absl::GetFlag( FLAGS_debug ) ) {
    std::cout << "DEBUG MODE: registering an stdout stats exporter only" << std::endl;
    opencensus::exporters::stats::StdoutExporter::Register();

  } else {
    std::cout << "PROD MODE: registering a stackdriver exporter only" << std::endl;

    opencensus::exporters::stats::StackdriverOptions statsOps;


    statsOps.project_id = absl::GetFlag( FLAGS_project_id ) ;
    statsOps.metric_name_prefix = "custom.googleapis.com/slb/zenith/";

    statsOps.monitored_resource.set_type( kGCEInstanceMonitoredResource );
    //statsOps.monitored_resource.mutable_labels()["project_id" ] = ;
    (*statsOps.monitored_resource.mutable_labels())[ kGCEInstanceIDLabel ] = absl::GetFlag( FLAGS_instance_id );
    (*statsOps.monitored_resource.mutable_labels())[ kZoneLabel ] = absl::GetFlag( FLAGS_zone) ;


    if ( statsOps.project_id.empty() ){
      std::cerr << "need to obtain stackdriver project id from metadata server\n";
      return 1;
    }

    opencensus::exporters::stats::StackdriverExporter::Register( std::move( statsOps ) );

  }
  std::cout << "parsing /proc/meminfo every " << absl::GetFlag( FLAGS_period_seconds ) << " seconds " << std::endl;

  bool firstTime = true;

  std::map< std::string, std::int64_t > byteValues, countValues;
  std::map< std::string, std::pair< opencensus::stats::MeasureInt64, std::int64_t > > byteMeasures, countMeasures;

  re2::RE2 static const countRe( "(.+):\\s+([[:alnum:]]+)" );
  re2::RE2 static const byteRe ( "(.+):\\s+([[:alnum:]]+) kB");

  while ( true ){

    std::ifstream ifile( "/proc/meminfo" );

    for ( std::string line; std::getline( ifile, line ); ){

      std::string label;
      std::int64_t count;

      if        ( re2::RE2::FullMatch( line,  byteRe, &label, &count ) ){
         byteValues[label] = count * 1024 ;
      } else if ( re2::RE2::FullMatch( line, countRe, &label, &count ) ){
        countValues[label] = count;
      }
    }

    if ( firstTime ){

      // how od these show up in stack driver monitoring ?


      for ( auto const & p : byteValues ){
        // register a measure
        auto mn = absl::StrCat( "proc/meminfo/", p.first ); // it turns out this string odes not show up in stackdriver monitoring
        opencensus::stats::MeasureInt64 m( opencensus::stats::MeasureInt64::Register( mn, p.first, "bytes" ) );

        // register a view ?
        auto vn = absl::StrCat( "proc/meminfo/", p.first , "_view"); // it turns out this string shows up in stackdriver monitoring....
        auto vd = opencensus::stats::ViewDescriptor()
          .set_name( vn )
          .set_measure( mn )
          .set_aggregation( opencensus::stats::Aggregation::LastValue())
          .set_description( absl::StrCat( p.first, " in bytes as per /proc/meminfo" ) );

        vd.RegisterForExport();
        byteMeasures.insert( { p.first,  { m,  byteValues.at(p.first) } } );
      }

      for ( auto const & p : countValues ){
        auto mn = absl::StrCat( "proc/meminfo/", p.first );
        opencensus::stats::MeasureInt64 m( opencensus::stats::MeasureInt64::Register( mn, p.first, "") );

        auto vn = absl::StrCat( "proc/meminfo/", p.first , "_view");
        auto vd = opencensus::stats::ViewDescriptor()
          .set_name( vn )
          .set_measure( mn )
          .set_aggregation( opencensus::stats::Aggregation::LastValue())
          .set_description( absl::StrCat( p.first, " count as per /proc/meminfo" ) );

        vd.RegisterForExport();

        countMeasures.insert( { p.first,  { m, countValues.at(p.first) } } );
      }

      firstTime = false;
    }

    for ( auto & p : byteMeasures ){
      opencensus::stats::Record( {{ p.second.first, byteValues.at(p.first) }} );
    }

    for ( auto  & p : countMeasures ){
      opencensus::stats::Record( {{ p.second.first, countValues.at(p.first) }} );
    }

    absl::SleepFor( absl::Seconds( absl::GetFlag( FLAGS_period_seconds  ) ) );

  }

  return 0;
}
