/**
 * Copyright (c) 2016-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CAFFE2_UTILS_MKL_OPERATOR_H_
#define CAFFE2_UTILS_MKL_OPERATOR_H_

#include "caffe2/core/operator.h"
#include "caffe2/mkl/utils/mkl_dnn_cppwrapper.h"
#include "caffe2/mkl/utils/mkl_memory.h"
#include "caffe2/proto/caffe2.pb.h"

namespace caffe2 {

CAFFE_DECLARE_REGISTRY(
    MKLOperatorRegistry,
    OperatorBase,
    const OperatorDef&,
    Workspace*);
#define REGISTER_MKL_OPERATOR_CREATOR(key, ...) \
  CAFFE_REGISTER_CREATOR(MKLOperatorRegistry, key, __VA_ARGS__)
#define REGISTER_MKL_OPERATOR(name, ...) \
  CAFFE_REGISTER_CLASS(MKLOperatorRegistry, name, __VA_ARGS__)
#define REGISTER_MKL_OPERATOR_STR(str_name, ...) \
  CAFFE_REGISTER_TYPED_CLASS(MKLOperatorRegistry, str_name, __VA_ARGS__)

#define REGISTER_MKL_OPERATOR_WITH_ENGINE(name, engine, ...) \
  CAFFE_REGISTER_CLASS(MKLOperatorRegistry, name##_ENGINE_##engine, __VA_ARGS__)

namespace mkl {
// MKLOperator is the base scaffolding of the operators that uses MKLDNN. It
// provides a few operators that are useful to MKLDNN specific implementations.
template <typename T>
class MKLOperator : public OperatorBase {
 public:
  explicit MKLOperator(const OperatorDef& operator_def, Workspace* ws)
      : OperatorBase(operator_def, ws),
        context_(operator_def.device_option()) {}
  virtual ~MKLOperator() {}

  inline const MKLMemory<T>& Input(int idx) {
    return OperatorBase::template Input<MKLMemory<T>>(idx);
  }
  inline MKLMemory<T>* Output(int idx) {
    return OperatorBase::template Output<MKLMemory<T>>(idx);
  }

  // The run function of Operator switches to the device, and then carries out
  // the actual computation with RunOnDevice(). You should implement RunOnDevice
  // instead of Run().
  bool Run(int /* unused */ /*stream_id*/) final {
    // Since MKLDNN does not need to do SwithToDevice and
    // FinishDeviceComputation,
    // it is always just a re-route to RunOnDevice().
    try {
      auto result = RunOnDevice();
      if (result) {
        event().SetFinished();
      } else {
        RecordEvent(getErrorMsg().c_str());
      }
      return result;
    } catch (EnforceNotMet& err) {
      err.AppendMessage(getErrorMsg());
      RecordEvent(err.what());
      throw;
    }
  }

  // Waits for a previous event. Note that to properly wait and run
  // asynchronously, WaitEvent, RunAsync and Record should all be executed
  // on the same CPU thread.
  void WaitEvent(const Event& ev, int /* unused */) final {
    context_.WaitEvent(ev);
  }

  void WaitEvents(const std::vector<const Event*>& events, int /* unused */)
      final {
    for (const auto& ev : events) {
      context_.WaitEvent(*ev);
    }
  }

  void RecordEvent(const char* err_msg = nullptr) final {
    if (event_) {
      context_.Record(event_.get(), err_msg);
    }
  }

  virtual bool RunOnDevice() = 0;

  inline void ExecutePrimitive() {
    MKLDNN_SAFE_CALL(mkl::dnnExecute<T>(primitive_, resources_));
  }

 protected:
  std::string getErrorMsg() {
    if (has_debug_def()) {
      return "Error from operator: " + ProtoDebugString(debug_def());
    } else {
      return "Error from operator: no op def";
    }
  }

  MKLContext context_;
  // The primitive used in the operator.
  PrimitiveWrapper<T> primitive_;
  // Size cache for all the input sizes.
  vector<vector<TIndex>> input_size_cache_;
  // An internal MKLMemory buffer. This is usually handy when we have a
  // single output from the operator. If your operator has multiple outputs
  // then you should allocate your own buffer.
  MKLMemory<T> buffer_;
  // The resources vector that we will need to use;
  void* resources_[dnnResourceNumber];
};
} // namespace mkl

#define USE_MKLOPERATOR_FUNCTIONS(T)                            \
  USE_OPERATOR_BASE_FUNCTIONS;                                  \
  /* using override */ using MKLOperator<T>::Input;             \
  /* using override */ using MKLOperator<T>::Output;            \
  /* using override */ using MKLOperator<T>::ExecutePrimitive;  \
  /* using override */ using MKLOperator<T>::primitive_;        \
  /* using override */ using MKLOperator<T>::input_size_cache_; \
  /* using override */ using MKLOperator<T>::buffer_;           \
  /* using override */ using MKLOperator<T>::resources_

#define USE_SIMPLE_MKL_CTOR_DTOR(name, T)              \
  name(const OperatorDef& operator_def, Workspace* ws) \
      : MKLOperator<T>(operator_def, ws) {}            \
  virtual ~name() {}

} // namespace caffe2

#endif // CAFFE2_UTILS_MKL_OPERATOR_H_
