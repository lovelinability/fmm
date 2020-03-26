/**
 * Content
 * Definition of a TrajectoryReader which is a wrapper of
 * the standard shapefile reader in GDAL.
 *
 * @author: Can Yang
 * @version: 2017.11.11
 */
#include "io/trajectory_reader.hpp"
#include "util/debug.hpp"
#include <iostream>
#include <string>

namespace MM
{
namespace IO
{

std::vector<Trajectory> ITrajectoryReader::read_next_N_trajectories(int N) {
  std::vector<Trajectory> trajectories;
  int i = 0;
  while(i<N && has_next_feature()) {
    trajectories.push_back(read_next_trajectory());
    ++i;
  }
  return trajectories;
}

std::vector<Trajectory> ITrajectoryReader::read_all_trajectories(){
  std::vector<Trajectory> trajectories;
  int i = 0;
  while(has_next_feature()) {
    trajectories.push_back(read_next_trajectory());
    ++i;
  }
  return trajectories;
}
/**
 *  According to the documentation at http://gdal.org/1.11/ogr/ogr_apitut.html
 *
 *  Note that OGRFeature::GetGeometryRef() and OGRFeature::GetGeomFieldRef()
 *  return a pointer to the internal geometry owned by the OGRFeature.
 *  We don't actually need to delete the return geometry. However, the
 *  OGRLayer::GetNextFeature() method returns a copy of the feature that is
 *  now owned by us. So at the end of use we must free the feature.
 *
 *  It implies that when we delete the feature, the geometry returned by
 *  OGRFeature::GetGeometryRef() is also deleted. Therefore, we need to
 *  create a copy of the geometry and free it with
 *      OGRGeometryFactory::destroyGeometry(geometry_pointer);
 *
 */

/**
 *  Constructor of TrajectoryReader
 *  @param filename, a GPS ESRI shapefile path
 *  @param id_name, the ID column name in the GPS shapefile
 */
GDALTrajectoryReader::GDALTrajectoryReader(const std::string & filename,
                                           const std::string & id_name)
{
  SPDLOG_INFO("Read trajectory from file {} with id column {}",
              filename, id_name)
  OGRRegisterAll();
  poDS = (GDALDataset*) GDALOpenEx(filename.c_str(),
                                   GDAL_OF_VECTOR, NULL, NULL, NULL );
  if( poDS == NULL )
  {
    SPDLOG_CRITICAL("Open data source fail")
    exit( 1 );
  }
  ogrlayer = poDS->GetLayer(0);
  _cursor=0;
  // Get the number of features first
  OGRFeatureDefn *ogrFDefn = ogrlayer->GetLayerDefn();
  // This should be a local field rather than a new variable
  id_idx=ogrFDefn->GetFieldIndex(id_name.c_str());
  NUM_FEATURES= ogrlayer->GetFeatureCount();
  if (id_idx<0)
  {
    SPDLOG_CRITICAL("Id column {} not found",id_name)
    GDALClose( poDS );
    std::exit(EXIT_FAILURE);
  }
  if (wkbFlatten(ogrFDefn->GetGeomType()) != wkbLineString)
  {
    SPDLOG_CRITICAL("Geometry type is {}, which should be linestring",
                    OGRGeometryTypeToName(ogrFDefn->GetGeomType()))
    GDALClose( poDS );
    std::exit(EXIT_FAILURE);
  } else {
    SPDLOG_INFO("Geometry type is {}",
                OGRGeometryTypeToName(ogrFDefn->GetGeomType()))
  }
  SPDLOG_INFO("Total number of trajectories {}", NUM_FEATURES)
  SPDLOG_INFO("Finish reading meta data")
}
// If there are still features not read
bool GDALTrajectoryReader::has_next_feature()
{
  return _cursor<NUM_FEATURES;
}
// Read the next trajectory in the shapefile
Trajectory GDALTrajectoryReader::read_next_trajectory()
{
  OGRFeature *ogrFeature =ogrlayer->GetNextFeature();
  int trid = ogrFeature->GetFieldAsInteger(id_idx);
  OGRGeometry *rawgeometry = ogrFeature->GetGeometryRef();
  LineString linestring = ogr2linestring((OGRLineString*) rawgeometry);
  OGRFeature::DestroyFeature(ogrFeature);
  ++_cursor;
  return Trajectory{trid,linestring};
}

// Get the number of trajectories in the file
int GDALTrajectoryReader::get_num_trajectories()
{
  return NUM_FEATURES;
}

void GDALTrajectoryReader::close(){
  GDALClose( poDS );
}




CSVTrajectoryReader::CSVTrajectoryReader(const std::string &e_filename,
                                         const std::string &id_name,
                                         const std::string &geom_name) :
  ifs(e_filename){
  std::string line;
  std::getline(ifs, line);
  std::stringstream check1(line);
  std::string intermediate;
  // Tokenizing w.r.t. space ' '
  int i = 0;
  while(getline(check1, intermediate, delim))
  {
    if (intermediate==id_name) {
      id_idx = i;
    }
    if (intermediate==geom_name) {
      geom_idx = i;
    }
    ++i;
  }
  if (id_idx<0 ||geom_idx<0) {
    SPDLOG_CRITICAL("Id {} or Geometry column {} not found",
                    id_name,geom_name)
    std::exit(EXIT_FAILURE);
  }
  SPDLOG_INFO("Id index {} Geometry index {}",id_idx,geom_idx)
}

Trajectory CSVTrajectoryReader::read_next_trajectory(){
  // Read the geom idx column into a trajectory
  std::string line;
  std::getline(ifs, line);
  std::stringstream ss(line);
  int trid = 0;
  int index = 0;
  std::string intermediate;
  LineString geom;
  while (std::getline(ss,intermediate,delim)) {
    if (index == id_idx) {
      trid = std::stoi(intermediate);
    }
    if (index == geom_idx) {
      // intermediate
      boost::geometry::read_wkt(intermediate,geom.get_geometry());
    }
    ++index;
  }
  return Trajectory{trid,geom};
}

bool CSVTrajectoryReader::has_next_feature() {
  return ifs.peek() != EOF;
}

void CSVTrajectoryReader::reset_cursor(){
  ifs.clear();
  ifs.seekg(0, std::ios::beg);
  std::string line;
  std::getline(ifs, line);
}
void CSVTrajectoryReader::close(){
  ifs.close();
}


CSVTemporalTrajectoryReader::CSVTemporalTrajectoryReader(
  const std::string &e_filename, const std::string &id_name,
  const std::string &geom_name, const std::string &time_name) :
  ifs(e_filename){
  std::string line;
  std::getline(ifs, line);
  std::stringstream check1(line);
  std::string intermediate;
  // Tokenizing w.r.t. space ' '
  int i = 0;
  while(getline(check1, intermediate, delim))
  {
    if (intermediate == id_name) {
      id_idx = i;
    }
    if (intermediate == geom_name) {
      geom_idx = i;
    }
    if (intermediate == time_name) {
      time_idx = i;
    }
    ++i;
  }
  if (id_idx<0 || geom_idx<0) {
    if (id_idx<0) {
      SPDLOG_CRITICAL("Id column {} not found",id_name)
    }
    if (geom_idx<0) {
      SPDLOG_CRITICAL("Geom column {} not found",geom_name)
    }
    std::exit(EXIT_FAILURE);
  }
  if (time_idx<0) {
    SPDLOG_WARN("Time stamp {} not found, will be estimated ",time_name)
  }
  SPDLOG_INFO("Id index {} Geometry index {} Time index {}",
      id_idx,geom_idx,time_idx)
}

TemporalTrajectory CSVTemporalTrajectoryReader::read_next_temporal_trajectory(){
  // Read the geom idx column into a trajectory
  std::string line;
  std::getline(ifs, line);
  std::stringstream ss(line);
  int trid =0;
  int index=0;
  std::string intermediate;
  LineString geom;
  std::vector<double> timestamps;
  while (std::getline(ss,intermediate,delim)) {
    if (index == id_idx) {
      trid = std::stoi(intermediate);
    }
    if (index == geom_idx) {
      // intermediate
      boost::geometry::read_wkt(intermediate,geom.get_geometry());
    }
    if (index == time_idx) {
      // intermediate
      timestamps = string2time(intermediate);
    }
    ++index;
  }
  return TemporalTrajectory{trid,geom,timestamps};
}

Trajectory CSVTemporalTrajectoryReader::read_next_trajectory(){
  // Read the geom idx column into a trajectory
  std::string line;
  std::getline(ifs, line);
  std::stringstream ss(line);
  int trid =0;
  int index=0;
  std::string intermediate;
  LineString geom;
  while (std::getline(ss,intermediate,delim)) {
    if (index == id_idx) {
      trid = std::stoi(intermediate);
    }
    if (index == geom_idx) {
      // intermediate
      boost::geometry::read_wkt(intermediate,geom.get_geometry());
    }
    ++index;
  }
  return Trajectory{trid,geom};
}

std::vector<double> CSVTemporalTrajectoryReader::string2time(
  const std::string &str){
  std::vector<double> values;
  std::stringstream ss(str);
  double v;
  while (ss >> v)
  {
    values.push_back(v);
    if (ss.peek()==',')
      ss.ignore();
  }
  return values;
}
bool CSVTemporalTrajectoryReader::has_next_feature() {
  return ifs.peek() != EOF;
}
void CSVTemporalTrajectoryReader::reset_cursor(){
  ifs.clear();
  ifs.seekg(0, std::ios::beg);
  std::string line;
  std::getline(ifs, line);
}
void CSVTemporalTrajectoryReader::close(){
  ifs.close();
}

bool CSVTemporalTrajectoryReader::has_time_stamp() const {
  return time_idx!=-1;
}

CSVTemporalPointReader::CSVTemporalPointReader(const std::string &e_filename,
                                               const std::string &id_name,
                                               const std::string &x_name,
                                               const std::string &y_name,
                                               const std::string &time_name):
  ifs(e_filename){
  std::string line;
  std::getline(ifs, line);
  std::stringstream check1(line);
  std::string intermediate;
  // Tokenizing w.r.t. space ' '
  int i = 0;
  while(getline(check1, intermediate, delim))
  {
    if (intermediate == id_name) {
      id_idx = i;
    }
    if (intermediate == x_name) {
      x_idx = i;
    }
    if (intermediate == y_name) {
      y_idx = i;
    }
    if (intermediate == time_name) {
      time_idx = i;
    }
    ++i;
  }
  if (id_idx<0 || x_idx<0 || y_idx<0) {
    if (id_idx<0) {
      SPDLOG_CRITICAL("Id column {} not found",id_name)
    }
    if (x_idx<0) {
      SPDLOG_CRITICAL("Geom column {} not found",x_name)
    }
    if (y_idx<0) {
      SPDLOG_CRITICAL("Geom column {} not found",y_name)
    }
    std::exit(EXIT_FAILURE);
  }
  if (time_idx<0) {
    SPDLOG_WARN("Time stamp {} not found, will be estimated ",time_name)
  }
  SPDLOG_INFO("Id index {} x index {} y index {} time index {}",
              id_idx,x_idx,y_idx,time_idx)
}

TemporalTrajectory CSVTemporalPointReader::read_next_temporal_trajectory() {
  // Read the geom idx column into a trajectory
  std::string intermediate;
  LineString geom;
  std::vector<double> timestamps;
  bool on_same_trajectory = true;
  bool first_observation = true;
  int prev_id = -1;
  double prev_timestamp = -1.0;
  std::string line;
  while (on_same_trajectory && has_next_feature()) {
    if (prev_line.empty()){
      std::getline(ifs, line);
    } else {
      line = prev_line;
      prev_line.clear();
    }
    std::stringstream ss(line);
    int id = 0;
    double x=0,y=0;
    double timestamp = 0;
    int index=0;
    while (std::getline(ss,intermediate,delim)) {
      if (index == id_idx) {
        id = std::stoi(intermediate);
      }
      if (index == x_idx) {
        x = std::stof(intermediate);
      }
      if (index == y_idx) {
        y = std::stof(intermediate);
      }
      if (index == time_idx) {
        timestamp = std::stof(intermediate);
      }
      ++index;
    }
    if (prev_id==id || first_observation){
      geom.add_point(x,y);
      if (has_time_stamp())
        timestamps.push_back(timestamp);
    }
    if (prev_id!=id && !first_observation) {
       on_same_trajectory = false;
    }
    first_observation = false;
    prev_id = id;
    if (!on_same_trajectory){
      prev_line = line;
    }
  }
  return TemporalTrajectory{prev_id,geom,timestamps};
}

Trajectory CSVTemporalPointReader::read_next_trajectory(){
  // Read the geom idx column into a trajectory
  std::string intermediate;
  LineString geom;
  bool on_same_trajectory = true;
  bool first_observation = true;
  int prev_id = -1;
  std::string line;
  while (on_same_trajectory && has_next_feature()) {
    if (prev_line.empty()){
      std::getline(ifs, line);
    } else {
      line = prev_line;
      prev_line.clear();
    }
    std::stringstream ss(line);
    int id = 0;
    double x=0,y=0;
    int index=0;
    while (std::getline(ss,intermediate,delim)) {
      if (index == id_idx) {
        id = std::stoi(intermediate);
      }
      if (index == x_idx) {
        x = std::stof(intermediate);
      }
      if (index == y_idx) {
        y = std::stof(intermediate);
      }
      ++index;
    }
    if (prev_id==id || first_observation){
      geom.add_point(x,y);
    }
    if (prev_id!=id && !first_observation) {
      on_same_trajectory = false;
    }
    first_observation = false;
    prev_id = id;
    if (!on_same_trajectory){
      prev_line = line;
    }
  }
  return Trajectory{prev_id,geom};
}

bool CSVTemporalPointReader::has_next_feature() {
  return ifs.peek() != EOF;
}

void CSVTemporalPointReader::reset_cursor() {
  ifs.clear();
  ifs.seekg(0, std::ios::beg);
  std::string line;
  std::getline(ifs, line);
}

void CSVTemporalPointReader::close() {
  ifs.close();
}

bool CSVTemporalPointReader::has_time_stamp() const {
  return time_idx!=-1;
}

} // IO
} // MM