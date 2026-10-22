#include <mc_nn/MCNNModel.h>

int main()
{
  MCNNModel model("", "cpu", "mcnn-consumer");
  return static_cast<int>(model.inputSize());
}
