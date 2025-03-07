#include <iostream>

#include "reduce.hh"

torch::Tensor reduce(torch::Tensor input, torch::Tensor keys, int64_t col) {
    /* Accumulates the entries in the first dimensions of the input tensors
     * according to the keys in column col with the same value
     *
     * @param input The tensor to be reduced @param keys The meta information
     * about the first dimension of the input @param col The column number of
     * key in keys to be used for the reduction @return The input tensor with
     * the accumulated entries in the first dimension
     */

    // unique is used differently on the c++ frontend
    // see https://stackoverflow.com/a/70809901
    // https://pytorch.org/cppdocs/api/function_namespaceat_1a70a940329a0c5d01c1f3e651f7acec98.html
    torch::Tensor key = keys.index({"...", col});
    torch::Tensor unique_entries, _ue_idx, _ue_count;
    std::tie(unique_entries, _ue_idx, _ue_count) = at::_unique2(key, true, false, false);

    std::vector<int64_t> reduced_shape = input.sizes().vec();
    reduced_shape[0] = unique_entries.sizes()[0];

    torch::Tensor indexes = torch::empty(
        {input.sizes()[0]},
        torch::TensorOptions()
            .dtype(torch::kInt32)
            .device(input.device())
    );

    for (int i = 0; i < unique_entries.sizes()[0]; i++) {
        auto idx = torch::where(key == unique_entries[i])[0];
        indexes.index_put_({idx}, i);
    }

    torch::Tensor reduced_input = torch::zeros(
        reduced_shape,
        torch::TensorOptions()
            .dtype(input.dtype())
            .device(input.device())
    );
    reduced_input.index_add_(0, indexes, input);
    // TODO replace for loop with cuda stuff
    // AT_DISPATCH_FLOATING_TYPES(gates.type(), "reduce_cuda_kernel", ([&] {
    //  reduce_cuda_kernel<scalar_t><<<blocks, threads>>>(
    //      input.data<scalar_t>(),
    //      reduced_input.data<scalar_t>(),
    //      state_size);
    //}));
    return reduced_input;
}

torch::autograd::variable_list ReduceAutograd::forward(
    torch::autograd::AutogradContext *ctx,
    torch::Tensor input,
    torch::Tensor keys,
    int64_t col
) {
    torch::Tensor key = keys.index({"...", col});

    torch::Tensor unique_entries, _ue_idx, _ue_count;
    std::tie(unique_entries, _ue_idx, _ue_count) = at::_unique2(key, true, false, false);

    std::vector<int64_t> reduced_shape = input.sizes().vec();
    reduced_shape[0] = unique_entries.sizes()[0];

    torch::Tensor indexes = torch::empty(
        {input.sizes()[0]},
        torch::TensorOptions()
            .dtype(torch::kInt32)
            .device(input.device())
    );

    auto reduce_mapping = std::vector<torch::Tensor>();
    for (int i = 0; i < unique_entries.sizes()[0]; i++) {
        auto idx = torch::where(key == unique_entries[i])[0];
        indexes.index_put_({idx}, i);
        reduce_mapping.push_back(idx);
    }

    torch::Tensor reduced_input = torch::zeros(
        reduced_shape,
        torch::TensorOptions()
            .dtype(input.dtype())
            .device(input.device())
    );
    reduced_input.index_add_(0, indexes, input);

    ctx->save_for_backward({input});
    ctx->saved_data["reduce_mapping"] = reduce_mapping;

    return {reduced_input};
}

torch::autograd::variable_list ReduceAutograd::backward(
    torch::autograd::AutogradContext *ctx,
    torch::autograd::variable_list outputs_grad
) {
    auto output_grad = outputs_grad[0];
    auto input = ctx->get_saved_variables()[0];
    auto reduce_mapping = ctx->saved_data["reduce_mapping"].toTensorVector();

    auto input_grad = torch::Tensor();
    if (input.requires_grad()) {
        input_grad = torch::zeros_like(input);

        for (int i=0; i<reduce_mapping.size(); i++) {
            input_grad.index_put_({reduce_mapping[i], "..."}, output_grad.index({i, "..."}));
        }
    }

    return {input_grad, torch::Tensor(), torch::Tensor()};
}

torch::Tensor reduce_custom_autograd(torch::Tensor input, torch::Tensor keys, int64_t col) {
    return ReduceAutograd::apply(input, keys, col)[0];
}
