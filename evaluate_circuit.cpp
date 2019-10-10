/**
 * @file evaluate_circuit.cpp
 * @see https://github.com/benjaminvillalonga/optimized_parallel_QC_with_TN
 *
 * @author Benjamin Villalonga (main contributor), Bron Nelson, Sergio Boixo and Salvatore Mandra
 * @date Created: August 2018
 * @date Modified: August 2018
 *
 * @copyright: Copyright © 2019, United States Government, as represented 
 * by the Administrator of the National Aeronautics and Space Administration. 
 * All rights reserved.
 * @licence: Apache License, Version 2.0
 */

#include "evaluate_circuit.h"

namespace qflex {

std::vector<std::vector<int>> read_grid_layout_from_stream(
    std::istream* grid_data, int I, int J) {
  if (grid_data == nullptr) {
    std::cout << "Grid data stream must be non-null." << std::endl;
    assert(grid_data != nullptr);
  }
  std::vector<std::vector<int>> qubits_off;
  bool on;
  for (int i = 0; i < I; ++i) {
    for (int j = 0; j < J; ++j) {
      bool data_at_end_of_file = grid_data->eof();
      if (data_at_end_of_file) {
        std::cout << "Grid layout file ran out at (" << i + 1 << "," << j + 1
                  << "). Expected size is (" << I << "," << J << ")."
                  << std::endl;
        assert(!data_at_end_of_file);
      }
      (*grid_data) >> on;
      if (on == 0) qubits_off.push_back({i, j});
    }
  }
  // Check for trailing whitespace.
  (*grid_data) >> on;
  bool leftover_qubits = !grid_data->eof();
  if (leftover_qubits) {
    std::cout << "Expected size is (" << I << "," << J << "), "
              << "but grid layout still has qubits left." << std::endl;
    assert(!leftover_qubits);
  }
  return qubits_off;
}

void get_output_states(const std::list<ContractionOperation>& ordering,
                       std::vector<std::vector<int>>* final_qubits,
                       std::vector<std::string>* output_states) {
  if (final_qubits == nullptr) {
    std::cout << "Final qubits must be non-null." << std::endl;
    assert(final_qubits != nullptr);
  }
  output_states->push_back("");
  std::vector<std::string> temp_output_states;
  for (const auto& op : ordering) {
    if (op.op_type != ContractionOperation::CUT) continue;
    // Any qubit with a terminal cut is in the final region.
    // TODO(martinop): update to use the new operation.
    if (op.cut.tensors.size() == 1) {
      final_qubits->push_back(op.cut.tensors[0]);
      for (const auto& state : *output_states) {
        std::vector<int> values = op.cut.values;
        if (values.empty()) values = {0, 1};
        for (const int value : values) {
          temp_output_states.push_back(state + std::to_string(value));
        }
      }
      *output_states = temp_output_states;
      temp_output_states.clear();
    }
  }
}

std::vector<std::pair<std::string, std::complex<double>>> EvaluateCircuit(
    QflexInput* input) {
  if (input == nullptr) {
    std::cout << "Input must be non-null." << std::endl;
    assert(input != nullptr);
  }
  // Set precision for the printed floats.
  std::cout.precision(12);

  // Timing variables.
  std::chrono::high_resolution_clock::time_point t_output_0, t_output_1;
  t_output_0 = std::chrono::high_resolution_clock::now();
  std::chrono::high_resolution_clock::time_point t0, t1;
  std::chrono::duration<double> time_span;

  // Reading input.
  const int super_dim = (int)pow(DIM, input->K);
  auto qubits_off =
      read_grid_layout_from_stream(input->grid_data, input->I, input->J);

  // Create the ordering for this tensor contraction from file.
  t0 = std::chrono::high_resolution_clock::now();
  std::list<ContractionOperation> ordering;
  ordering_data_to_contraction_ordering(input->ordering_data, input->I,
                                        input->J, qubits_off, &ordering);
  t1 = std::chrono::high_resolution_clock::now();
  time_span =
      std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0);
  if (input->enable_timing_logs) {
    std::cout << "Time spent making contraction ordering: " << time_span.count()
              << "s" << std::endl;
  }

  // Get a list of qubits and output states for the final region.
  std::vector<std::vector<int>> final_qubits;
  std::vector<std::string> output_states;
  get_output_states(ordering, &final_qubits, &output_states);

  int init_length = input->I * input->J - qubits_off.size();
  if (input->initial_state.empty()) {
    input->initial_state = std::string(init_length, '0');
  }
  if (input->final_state_A.empty()) {
    input->final_state_A = std::string(init_length - final_qubits.size(), '0');
  }

  // Scratch space to be reused for operations.
  t0 = std::chrono::high_resolution_clock::now();
  s_type* scratch = new s_type[(int)pow(super_dim, 7)];
  t1 = std::chrono::high_resolution_clock::now();
  time_span =
      std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0);
  if (input->enable_timing_logs) {
    std::cout << "Time spent reading allocating scratch space: "
              << time_span.count() << "s" << std::endl;
  }

  // Declaring and then filling 2D grid of tensors.
  std::vector<std::vector<Tensor>> tensor_grid(input->I);
  for (int i = 0; i < input->I; ++i) {
    tensor_grid[i] = std::vector<Tensor>(input->J);
  }
  // Scope so that the 3D grid of tensors is destructed.
  {
    // Creating 3D grid of tensors from file.
    t0 = std::chrono::high_resolution_clock::now();
    std::vector<std::vector<std::vector<Tensor>>> tensor_grid_3D;
    circuit_data_to_grid_of_tensors(input->circuit_data, input->I, input->J,
                                    input->K, input->initial_state,
                                    input->final_state_A, final_qubits,
                                    qubits_off, tensor_grid_3D, scratch);
    t1 = std::chrono::high_resolution_clock::now();
    time_span =
        std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0);
    if (input->enable_timing_logs) {
      std::cout << "Time spent creating 3D grid of tensors from file: "
                << time_span.count() << "s" << std::endl;
    }

    // Contract 3D grid onto 2D grid of tensors, as usual.
    t0 = std::chrono::high_resolution_clock::now();
    grid_of_tensors_3D_to_2D(tensor_grid_3D, tensor_grid, final_qubits,
                             qubits_off, ordering, scratch);
    t1 = std::chrono::high_resolution_clock::now();
    time_span =
        std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0);
    if (input->enable_timing_logs) {
      std::cout << "Time spent creating 2D grid of tensors from 3D one: "
                << time_span.count() << "s" << std::endl;
    }

    // Freeing scratch data: delete and NULL.
    delete[] scratch;
    scratch = NULL;
  }

  // Perform tensor grid contraction.
  std::vector<std::complex<double>> amplitudes(output_states.size());
  std::vector<std::pair<std::string, std::complex<double>>> result;
  ContractGrid(ordering, &tensor_grid, &amplitudes);
  for (int c = 0; c < amplitudes.size(); ++c) {
    result.push_back(std::make_pair(
        input->final_state_A + " " + output_states[c], amplitudes[c]));
  }

  // Final time
  t_output_1 = std::chrono::high_resolution_clock::now();
  time_span = std::chrono::duration_cast<std::chrono::duration<double>>(
      t_output_1 - t_output_0);
  std::cout << "Total time: " << time_span.count() << "s" << std::endl;

  return result;
}

}  // namespace qflex
