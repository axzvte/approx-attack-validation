#include "../app/circuit_ir.hpp"
#include <iostream>
#include <stdexcept>
#include <functional>
#define require(ok) do { if (!(ok)) throw std::runtime_error("IR assertion failed at line "+std::to_string(__LINE__)); } while(false)
static void rejectAt(const std::function<void()>& f,int line) {
 bool rejected=false; try { f(); } catch(const std::exception&) {rejected=true;}
 if(!rejected) throw std::runtime_error("Expected rejection at line "+std::to_string(line));
}
#define rejects(f) rejectAt(f,__LINE__)
static std::string scalar(const std::string& op,const std::string& extra="",const std::string& arg="-2.5") {
 return "{\"schema\":3,\"name\":\"scalar\",\"application\":\"ir\",\"baseline\":[],\"ir\":{\"schema\":1,\"tile\":[1,1],\"border\":\"reflect101\",\"interior_only\":false,\"inputs\":[{\"id\":\"p\",\"dx\":0,\"dy\":0}],\"operations\":[{\"id\":\"r\",\"op\":\""+op+"\",\"args\":["+arg+"],\"integer\":true,\"width\":32,\"signed\":true"+extra+"},{\"id\":\"a\",\"op\":\"add\",\"args\":[\"r\",128],\"integer\":true,\"width\":32,\"signed\":true}],\"outputs\":[{\"value\":\"a\",\"dx\":0,\"dy\":0}]}}";
}
int main() {
 try {
  const std::string json=R"({"schema":3,"name":"test","application":"ir","baseline":[{"node":7,"unit":"5RP"}],"ir":{"schema":1,"tile":[1,1],"border":"replicate","interior_only":false,"inputs":[{"id":"p","dx":0,"dy":0}],"operations":[{"id":"a","op":"add","args":["p",1],"integer":true,"width":32,"signed":true,"unit_node":7},{"id":"b","op":"add","args":["a",1],"integer":true,"width":32,"signed":true,"unit_node":7}],"outputs":[{"value":"b","dx":0,"dy":0}]}})";
  const auto p=circuit_ir::Profile::parse(json);
  cv::Mat input(2,3,CV_8UC1,cv::Scalar(10));
  auto exact=p.run(input,p.exactConfig()); require(exact.at<unsigned char>(0,0)==12);
  auto changed=p.run(input,p.exactConfig(),cv::Rect(0,0,1,1),"a",0);
  require(changed.at<unsigned char>(0,0)==11); require(changed.at<unsigned char>(0,1)==12);
  auto approx=p.run(input,p.baseline); require(approx.size()==input.size());
  bool rejected=false;
  try { p.run(input,p.baseline,cv::Rect(0,0,1,1),"missing",0); } catch(const std::exception&) { rejected=true; }
  require(rejected);
  require(cv::countNonZero(p.run(input,p.compensatedConfig({7}))!=exact)==0);
  rejects([&]{p.compensatedConfig({7,7});});
  for(const auto& test:std::vector<std::pair<std::string,int>>{
    {scalar("round_even"),126},{scalar("round_even","","2.5"),130},
    {scalar("trunc"),126},{scalar("shift",",\"bits\":1","-3"),126},
    {scalar("round_shift",",\"bits\":1","-5"),125},
    {scalar("round_shift",",\"bits\":1","5"),131},
    {scalar("shift",",\"bits\":63","-1"),127},
    {scalar("round_shift",",\"bits\":63","-1"),128}}) {
    const auto r=circuit_ir::Profile::parse(test.first);
    require(r.run(input,r.baseline).at<unsigned char>(0,0)==test.second);
  }
  const std::string tiled=R"({"schema":3,"name":"tile","application":"ir","baseline":[],"ir":{"schema":1,"tile":[2,2],"border":"replicate","interior_only":false,"inputs":[{"id":"p","dx":1,"dy":1}],"operations":[{"id":"site","op":"add","args":["p",1],"integer":true,"width":32,"signed":true}],"outputs":[{"value":"site","dx":0,"dy":0},{"value":"site","dx":1,"dy":0},{"value":"site","dx":0,"dy":1},{"value":"site","dx":1,"dy":1}]}})";
  const auto tile=circuit_ir::Profile::parse(tiled);
  const auto tiledPlain=tile.run(input,{});
  auto partial=tile.run(input,{},cv::Rect(1,0,2,2),"site",0);
  require(cv::countNonZero(partial!=tiledPlain)==0);
  auto full=tile.run(input,{},cv::Rect(0,0,2,2),"site",0);
  require(full.at<unsigned char>(0,0)==10 && full.at<unsigned char>(1,1)==10);
  require(full.at<unsigned char>(0,2)==11);
  auto bad=tiled; bad.replace(bad.rfind("\"dx\":1,\"dy\":1}]"),15,"\"dx\":0,\"dy\":0}]");
  rejects([&]{circuit_ir::Profile::parse(bad);});
  auto forward=json; forward.replace(forward.find("\"p\",1"),5,"\"b\",1");
  rejects([&]{circuit_ir::Profile::parse(forward);});
  auto fractional=circuit_ir::Profile::parse(scalar("scale",",\"factor\":0.5","1"));
  rejects([&]{fractional.run(input,{});});
  auto wide=circuit_ir::Profile::parse(scalar("scale",",\"factor\":2147483648","1"));
  rejects([&]{wide.run(input,{});});
  auto exceed=json; exceed.replace(exceed.find("\"p\",1"),5,"2048,1");
  const auto oversized=circuit_ir::Profile::parse(exceed);
  rejects([&]{oversized.run(input,oversized.baseline);});
  std::string interior=R"({"schema":3,"name":"border","application":"ir","baseline":[],"ir":{"schema":1,"tile":[1,1],"border":"replicate","interior_only":true,"border_output":"input","inputs":[{"id":"p","dx":-1,"dy":0}],"operations":[{"id":"site","op":"add","args":["p",1],"integer":true,"width":32,"signed":true}],"outputs":[{"value":"site","dx":0,"dy":0}]}})";
  const auto borders=circuit_ir::Profile::parse(interior);
  auto omittedBorder=interior;
  const std::string borderField="\"border_output\":\"input\",";
  omittedBorder.erase(omittedBorder.find(borderField),borderField.size());
  const auto implicitBorder=circuit_ir::Profile::parse(omittedBorder);
  require(implicitBorder.run(input,{}).at<unsigned char>(0,0)==10);
  const auto borderFlip=borders.run(input,{},cv::Rect(0,0,3,2),"site",1);
  require(borderFlip.at<unsigned char>(0,0)==10 && borderFlip.at<unsigned char>(0,1)==9);
  const auto borderPlain=borders.run(input,{});
  const auto fallback=borders.flipOutput(input,borderPlain,cv::Rect(0,0,3,2),0);
  require(fallback.at<unsigned char>(0,0)==10 && fallback.at<unsigned char>(0,1)==10);
  std::cout<<"IR execution passed\n";
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
