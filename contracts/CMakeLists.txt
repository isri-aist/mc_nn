# One directory per contract, each built as an optional library by
# mc_nn_add_contract (see ../cmake/MCNNContract.cmake). A contract linking to
# another one must be listed after it. Contracts can also live in their own
# project (see README.md, "External contracts").
add_subdirectory(RandomPolicyContract)
