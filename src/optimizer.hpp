#pragma once

#include "ops.hpp"
#include "tensor.hpp"

namespace tinytorch {

struct SGDOptimizer  // stochastic gradient descent
{
  int iter = 0;
  float momentum = 0.9;
  float dampening = 0.1;
  std::vector<Tensor> params;
  std::vector<Tensor> velocities;
  float lr;
  bool nesterov = true;

  SGDOptimizer(std::vector<Tensor> t_lst, float lr) : params(t_lst), lr(lr) {
    // initialize velocities
    velocities.resize(t_lst.size());
    for (size_t i = 0; i < t_lst.size(); ++i) {
      velocities[i] = zeros(t_lst[i].shape(), t_lst[i].device());
    }
  }

  void zeroGrad() {
    for (Tensor &t : params) {
      if (t.grad().defined()) {
        t.grad().zero_();
      }
    }
  }

  void step() {
    // update the weight of params
    // sgd with nesterov momentum, equation from pytorch
    // see https://pytorch.org/docs/stable/generated/torch.optim.SGD.html
    for (size_t p = 0; p < params.size(); p++) {
      Tensor &param = params[p];
      if (!param.grad().defined()) {
        continue;
      }
      // TORCH_CHECK(param.shape().size() == 1, "param shape size should be 1");
      TORCH_CHECK(param.grad().numel() == param.numel(),
                  "grad size and size should be equal.");
      Tensor grad = param.grad();
      if (momentum != 0) {
        if (iter > 0) {
          velocities[p] = velocities[p] * momentum + grad * (1 - dampening);
        } else {
          velocities[p] = grad;
        }

        if (nesterov) {
          grad += velocities[p] * momentum;
        } else {
          grad = velocities[p];
        }
      }
      param -= grad * lr;
    }
    iter++;
  }
};

}  // namespace tinytorch
