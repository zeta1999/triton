﻿#include <pybind11/pybind11.h>
#include <pybind11/buffer_info.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <string>
#include "triton/runtime/function.h"
#include "triton/runtime/arg.h"
#include "triton/lang/code_gen.h"
#include "triton/lang/parser.h"
#include "triton/lang/cpp.h"
#include "triton/ir/module.h"
#include "triton/ir/function.h"

using namespace triton;

namespace rt = triton::runtime;

typedef std::pair<int, int> map_key_t;
std::map<map_key_t, std::shared_ptr<rt::function::grid_fn_ty>> id_grid_map;
std::map<map_key_t, std::shared_ptr<rt::function>> id_fn_map;
std::map<size_t, double> fp64scalar_map;
std::map<size_t, int64_t> i64scalar_map;

/* Grid map */

void register_grid(const map_key_t& key,
                   const rt::function::grid_fn_ty& grid_fn) {
  id_grid_map[key].reset(new rt::function::grid_fn_ty(grid_fn));
}

void delete_grid(const map_key_t& key) {
  id_grid_map.erase(key);
}

/* Function map */

void register_fn(const map_key_t& key,
                 const std::string& src,
                 const rt::function::options_space_t& opt,
                 const std::string &cache_ref) {
  id_fn_map[key].reset(new rt::function(src, opt, cache_ref));
}

void delete_fn(const map_key_t& key) {
  id_fn_map.erase(key);
}

void register_cst(const map_key_t& key, const std::string& name, pybind11::buffer& data) {
  pybind11::buffer_info info = data.request();
  id_fn_map[key]->set_cst(name, info.ptr, info.size*info.itemsize);
}

void cleanup() {
  id_grid_map.clear();
  id_fn_map.clear();
  i64scalar_map.clear();
}

size_t make_op_id() {
  return id_fn_map.size();
}


/* TF scalar wrapper */
size_t make_scalar_id() {
  size_t ret = i64scalar_map.size();
  i64scalar_map[ret] = int64_t();
  return ret;
}

bool has_scalar(size_t id) {
  return i64scalar_map.find(id) != i64scalar_map.end();
}

int64_t retrieve_scalar(size_t id) {
  return i64scalar_map.at(id);
}

void make_module(const std::string& src, ir::module* ir,
                 const runtime::function::options_space_t& opt) {
  std::string copy = triton::runtime::function::preheader() + src;
  // pre-process
  TokenSequence tokens;
  Preprocessor cpp(&copy, true);
  for(auto it: opt.defines){
    cpp.AddMacro(it.first, &it.second[0]);
  }
  cpp.Process(tokens);
  // parse
  Parser parser(tokens);
  parser.Parse();
  Generator gen(&parser);
  gen.Gen(ir);
}

/* Function signature */
std::vector<rt::arg_type> get_fn_signature(const std::string& src,
                                           const runtime::function::options_space_t& opt) {
  // triton-ir code-gen
  ir::context ctx;
  auto ir = std::shared_ptr<ir::module>(new ir::module("", ctx));
  make_module(src, &*ir, opt);
  // function
  ir::function* fn = ir->get_function_list().front();
  // extract signature
  std::vector<rt::arg_type> ret;
  ir::function_type* ty = fn->get_fn_type();
  for(size_t i = 0; i < ty->get_num_params(); i++)
    ret.push_back(rt::convert(ty->get_param_ty(i)));
  return ret;
}

typedef triton::runtime::function::options_t options_t;
typedef triton::runtime::function::options_space_t options_space_t;

PYBIND11_MODULE(libtriton, m) {
    m.doc() = "Python bindings to the C++ Triton API";

    // bindings for triton classes
    pybind11::enum_<rt::arg_type>(m, "arg_type")
        .value("int1", rt::INT1_T)
        .value("int8", rt::INT8_T)
        .value("int16", rt::INT16_T)
        .value("int32", rt::INT32_T)
        .value("int64", rt::INT64_T)
        .value("half", rt::HALF_T)
        .value("float", rt::FLOAT_T)
        .value("double", rt::DOUBLE_T)
        .value("buffer", rt::BUFFER_T);

    pybind11::class_<options_t>(m, "options")
        .def(pybind11::init<>())
        .def("d", &options_t::D<int>)
        .def_readonly("num_warps", &options_t::num_warps);

    pybind11::class_<options_space_t>(m, "options_space")
        .def(pybind11::init<>())
        .def_readwrite("defines", &options_space_t::defines)
        .def_readwrite("num_warps", &options_space_t::num_warps);

    // hooks into triton constructs since frameworks may not use pybind11
    m.def("get_fn_signature", &get_fn_signature);
    m.def("register_grid", &register_grid);
    m.def("delete_grid", &delete_grid);
    m.def("register_fn", &register_fn);
    m.def("register_cst", &register_cst);
    m.def("delete_fn", &delete_fn);
    m.def("make_op_id", &make_op_id);
    m.def("make_scalar_id", &make_scalar_id);
    m.def("retrieve_scalar", &retrieve_scalar);
    m.def("cleanup", &cleanup);
    ;
}
