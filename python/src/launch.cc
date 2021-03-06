// Thanks to Scott Gray (OpenAI) for the idea to pass the arguments
// as a string constructed with struct.pack in python

#include "triton/driver/buffer.h"
#include "triton/driver/stream.h"
#include "triton/runtime/function.h"
#include "triton/tools/bench.hpp"
#include "torch/script.h"
#include "ATen/cuda/CUDAContext.h"

namespace rt = triton::runtime;
namespace drv = triton::driver;

typedef std::pair<int, int> map_key_t;
extern std::map<map_key_t, std::shared_ptr<rt::function::grid_fn_ty>> id_grid_map;
extern std::map<map_key_t, std::shared_ptr<rt::function>> id_fn_map;
std::shared_ptr<drv::device> host_device;
std::shared_ptr<drv::context> host_context;
std::shared_ptr<drv::stream> host_stream;

void launch_kernel(int64_t op_id, int64_t dev_id, const std::string& args){
  if(dev_id == -1){
    if(!host_stream){
      host_device.reset(new drv::host_device());
      host_context.reset(drv::context::create(&*host_device));
      host_stream.reset(drv::stream::create(&*host_context));
    }
    (*id_fn_map.at({op_id, dev_id}))((void**)args.c_str(), args.size(), *id_grid_map.at({op_id, dev_id}), &*host_stream);
  }
  else{
    CUstream custream = (CUstream)at::cuda::getCurrentCUDAStream(dev_id).stream();
    triton::driver::cu_stream stream(custream, false);
    triton::driver::context* ctx = stream.context();
    (*id_fn_map.at({op_id, dev_id}))((void**)args.c_str(), args.size(), *id_grid_map.at({op_id, dev_id}), &stream);
  }
}


static auto registry = torch::RegisterOperators("triton::launch_kernel", &launch_kernel);
