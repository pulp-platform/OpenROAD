#include "rmp/block_placement.h"

#include <float.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

#include "rmp/shape_engine.h"
#include "rmp/util.h"
#include "utl/Logger.h"

namespace block_placement {
using std::abs;
using std::cout;
using std::endl;
using std::exp;
using std::fstream;
using std::getline;
using std::ios;
using std::log;
using std::max;
using std::min;
using std::ofstream;
using std::pair;
using std::pow;
using std::sort;
using std::sqrt;
using std::stof;
using std::stoi;
using std::string;
using std::swap;
using std::thread;
using std::to_string;
using std::unordered_map;
using std::vector;
using utl::Logger;
using utl::RMP;

void SimulatedAnnealingCore::PackFloorplan()
{
  for (int i = 0; i < blocks_.size(); i++) {
    blocks_[i].SpecifyX(0.0);
    blocks_[i].SpecifyY(0.0);
  }

  // calculate X position
  pair<int, int>* match = new pair<int, int>[blocks_.size()];
  for (int i = 0; i < pos_seq_.size(); i++) {
    match[pos_seq_[i]].first = i;
    match[neg_seq_[i]].second = i;
  }

  float* length = new float[blocks_.size()];
  for (int i = 0; i < blocks_.size(); i++)
    length[i] = 0.0;

  for (int i = 0; i < pos_seq_.size(); i++) {
    int b = pos_seq_[i];
    int p = match[b].second;
    blocks_[b].SpecifyX(length[p]);
    float t = blocks_[b].GetX() + blocks_[b].GetWidth();
    for (int j = p; j < neg_seq_.size(); j++)
      if (t > length[j])
        length[j] = t;
      else
        break;
  }

  width_ = length[blocks_.size() - 1];

  // calulate Y position
  int* pos_seq = new int[pos_seq_.size()];
  int num_blocks = pos_seq_.size();
  for (int i = 0; i < num_blocks; i++)
    pos_seq[i] = pos_seq_[num_blocks - 1 - i];

  for (int i = 0; i < num_blocks; i++) {
    match[pos_seq[i]].first = i;
    match[neg_seq_[i]].second = i;
  }

  for (int i = 0; i < num_blocks; i++)
    length[i] = 0.0;

  for (int i = 0; i < num_blocks; i++) {
    int b = pos_seq[i];
    int p = match[b].second;
    blocks_[b].SpecifyY(length[p]);
    float t = blocks_[b].GetY() + blocks_[b].GetHeight();
    for (int j = p; j < num_blocks; j++)
      if (t > length[j])
        length[j] = t;
      else
        break;
  }

  height_ = length[num_blocks - 1];
  area_ = width_ * height_;
}

void SimulatedAnnealingCore::SingleSwap(bool flag)
{
  int index1 = (int) (floor((distribution_)(generator_) *blocks_.size()));
  int index2 = (int) (floor((distribution_)(generator_) *blocks_.size()));
  while (index1 == index2) {
    index2 = (int) (floor((distribution_)(generator_) *blocks_.size()));
  }

  if (flag)
    swap(pos_seq_[index1], pos_seq_[index2]);
  else
    swap(neg_seq_[index1], neg_seq_[index2]);
}

void SimulatedAnnealingCore::DoubleSwap()
{
  int index1 = (int) (floor((distribution_)(generator_) *blocks_.size()));
  int index2 = (int) (floor((distribution_)(generator_) *blocks_.size()));
  while (index1 == index2) {
    index2 = (int) (floor((distribution_)(generator_) *blocks_.size()));
  }

  swap(pos_seq_[index1], pos_seq_[index2]);
  swap(neg_seq_[index1], neg_seq_[index2]);
}

void SimulatedAnnealingCore::Resize()
{
  vector<int> hard_block_list;
  vector<int> soft_block_list;

  for (int i = 0; i < blocks_.size(); i++) {
    if (blocks_[i].GetNumMacro() > 1) {
      hard_block_list.push_back(i);
    } else {
      soft_block_list.push_back(i);
    }
  }

  int index1 = -1;
  float prob = (distribution_)(generator_);
  if (prob <= hard_block_list.size() / blocks_.size()) {
    index1 = hard_block_list[floor(
        (distribution_)(generator_) *hard_block_list.size())];
  } else {
    index1 = soft_block_list[floor(
        (distribution_)(generator_) *soft_block_list.size())];
  }

  // int index1 = (int)(floor((distribution_)(generator_) * blocks_.size()));

  while (blocks_[index1].IsResize() == false) {
    index1 = (int) (floor((distribution_)(generator_) *blocks_.size()));
  }

  block_id_ = index1;

  if (blocks_[index1].GetNumMacro() > 0) {
    blocks_[index1].ResizeHardBlock();
    return;
  }

  float option = (distribution_)(generator_);
  if (option <= 0.2) {
    // Change the aspect ratio of the soft block to a random value in the
    // range of the given soft aspect-ratio constraint
    blocks_[index1].ChooseAspectRatioRandom();
  } else if (option <= 0.4) {
    // Change the width of soft block to Rb = e.x2 - b.x1
    float b_x1 = blocks_[index1].GetX();
    float b_x2 = b_x1 + blocks_[index1].GetWidth();
    float e_x2 = outline_width_;

    if (b_x1 >= e_x2)
      return;

    for (int i = 0; i < blocks_.size(); i++) {
      float cur_x2 = blocks_[i].GetX() + blocks_[i].GetWidth();
      if (cur_x2 > b_x2 && cur_x2 < e_x2)
        e_x2 = cur_x2;
    }

    float width = e_x2 - b_x1;
    blocks_[index1].ChangeWidth(width);
  } else if (option <= 0.6) {
    // change the width of soft block to Lb = d.x2 - b.x1
    float b_x1 = blocks_[index1].GetX();
    float b_x2 = b_x1 + blocks_[index1].GetWidth();
    float d_x2 = b_x1;
    for (int i = 0; i < blocks_.size(); i++) {
      float cur_x2 = blocks_[i].GetX() + blocks_[i].GetWidth();
      if (cur_x2 < b_x2 && cur_x2 > d_x2)
        d_x2 = cur_x2;
    }

    if (d_x2 <= b_x1) {
      return;
    } else {
      float width = d_x2 - b_x1;
      blocks_[index1].ChangeWidth(width);
    }
  } else if (option <= 0.8) {
    // change the height of soft block to Tb = a.y2 - b.y1
    float b_y1 = blocks_[index1].GetY();
    float b_y2 = b_y1 + blocks_[index1].GetHeight();
    float a_y2 = outline_height_;

    if (b_y1 >= a_y2)
      return;

    for (int i = 0; i < blocks_.size(); i++) {
      float cur_y2 = blocks_[i].GetY() + blocks_[i].GetHeight();
      if (cur_y2 > b_y2 && cur_y2 < a_y2)
        a_y2 = cur_y2;
    }

    float height = a_y2 - b_y1;
    blocks_[index1].ChangeHeight(height);
  } else {
    // Change the height of soft block to Bb = c.y2 - b.y1
    float b_y1 = blocks_[index1].GetY();
    float b_y2 = b_y1 + blocks_[index1].GetHeight();
    float c_y2 = b_y1;
    for (int i = 0; i < blocks_.size(); i++) {
      float cur_y2 = blocks_[i].GetY() + blocks_[i].GetHeight();
      if (cur_y2 < b_y2 && cur_y2 > c_y2)
        c_y2 = cur_y2;
    }

    if (c_y2 <= b_y1) {
      return;
    } else {
      float height = c_y2 - b_y1;
      blocks_[index1].ChangeHeight(height);
    }
  }
}

void SimulatedAnnealingCore::Perturb()
{
  if (blocks_.size() == 1)
    return;

  pre_pos_seq_ = pos_seq_;
  pre_neg_seq_ = neg_seq_;
  pre_width_ = width_;
  pre_height_ = height_;
  pre_area_ = area_;
  pre_wirelength_ = wirelength_;
  pre_outline_penalty_ = outline_penalty_;
  pre_boundary_penalty_ = boundary_penalty_;
  pre_macro_blockage_penalty_ = macro_blockage_penalty_;

  float op = (distribution_)(generator_);
  if (op <= resize_prob_) {
    action_id_ = 0;
    pre_blocks_ = blocks_;
    Resize();
  } else if (op <= pos_swap_prob_) {
    action_id_ = 1;
    SingleSwap(true);
  } else if (op <= neg_swap_prob_) {
    action_id_ = 2;
    SingleSwap(false);
  } else {
    action_id_ = 3;
    DoubleSwap();
  }

  PackFloorplan();
}

void SimulatedAnnealingCore::Restore()
{
  // To reduce the running time, I didn't call PackFloorplan again
  // So when we write the final floorplan out, we need to PackFloor again
  // at the end of SA process
  if (action_id_ == 0)
    blocks_[block_id_] = pre_blocks_[block_id_];
  else if (action_id_ == 1)
    pos_seq_ = pre_pos_seq_;
  else if (action_id_ == 2)
    neg_seq_ = pre_neg_seq_;
  else {
    pos_seq_ = pre_pos_seq_;
    neg_seq_ = pre_neg_seq_;
  }

  width_ = pre_width_;
  height_ = pre_height_;
  area_ = pre_area_;
  wirelength_ = pre_wirelength_;
  outline_penalty_ = pre_outline_penalty_;
  boundary_penalty_ = pre_boundary_penalty_;
  macro_blockage_penalty_ = pre_macro_blockage_penalty_;
}

void SimulatedAnnealingCore::CalculateOutlinePenalty()
{
  outline_penalty_ = 0.0;

  float max_width = max(outline_width_, width_);
  float max_height = max(outline_height_, height_);
  outline_penalty_
      = max(outline_penalty_,
            max_width * max_height - outline_width_ * outline_height_);
}

void SimulatedAnnealingCore::CalculateMacroBlockagePenalty()
{
  macro_blockage_penalty_ = 0.0;
  if (regions_.size() == 0)
    return;

  vector<Region*>::iterator vec_iter = regions_.begin();
  for (vec_iter; vec_iter != regions_.end(); vec_iter++) {
    for (int i = 0; i < blocks_.size(); i++) {
      if (blocks_[i].GetNumMacro() > 0) {
        float lx = blocks_[i].GetX();
        float ly = blocks_[i].GetY();
        float ux = lx + blocks_[i].GetWidth();
        float uy = ly + blocks_[i].GetHeight();

        float region_lx = (*vec_iter)->lx_;
        float region_ly = (*vec_iter)->ly_;
        float region_ux = (*vec_iter)->ux_;
        float region_uy = (*vec_iter)->uy_;

        if (ux <= region_lx || lx >= region_ux || uy <= region_ly
            || ly >= region_uy)
          ;
        else {
          float width = min(ux, region_ux) - max(lx, region_lx);
          float height = min(uy, region_uy) - max(ly, region_ly);
          macro_blockage_penalty_ += width * height;
        }
      }
    }
  }
}

void SimulatedAnnealingCore::CalculateBoundaryPenalty()
{
  boundary_penalty_ = 0.0;
  for (int i = 0; i < blocks_.size(); i++) {
    int weight = blocks_[i].GetNumMacro();
    if (weight > 0) {
      float lx = blocks_[i].GetX();
      float ly = blocks_[i].GetY();
      float ux = lx + blocks_[i].GetWidth();
      float uy = ly + blocks_[i].GetHeight();

      lx = min(lx, abs(outline_width_ - ux));
      ly = min(ly, abs(outline_height_ - uy));
      // lx = min(lx, ly);
      lx = lx + ly;
      boundary_penalty_ += lx;
    }
  }
}

void SimulatedAnnealingCore::CalculateWirelength()
{
  wirelength_ = 0.0;
  std::vector<Net*>::iterator net_iter = nets_.begin();
  for (net_iter; net_iter != nets_.end(); net_iter++) {
    vector<string> blocks = (*net_iter)->blocks_;
    vector<string> terminals = (*net_iter)->terminals_;
    int weight = (*net_iter)->weight_;
    float lx = FLT_MAX;
    float ly = FLT_MAX;
    float ux = 0.0;
    float uy = 0.0;

    for (int i = 0; i < blocks.size(); i++) {
      float x = blocks_[block_map_[blocks[i]]].GetX()
                + blocks_[block_map_[blocks[i]]].GetWidth() / 2;
      float y = blocks_[block_map_[blocks[i]]].GetY()
                + blocks_[block_map_[blocks[i]]].GetHeight() / 2;
      lx = min(lx, x);
      ly = min(ly, y);
      ux = max(ux, x);
      uy = max(uy, y);
    }

    for (int i = 0; i < terminals.size(); i++) {
      float x = terminal_position_[terminals[i]].first;
      float y = terminal_position_[terminals[i]].second;
      lx = min(lx, x);
      ly = min(ly, y);
      ux = max(ux, x);
      uy = max(uy, y);
    }

    wirelength_ += (abs(ux - lx) + abs(uy - ly)) * weight;
  }
}

float SimulatedAnnealingCore::NormCost(float area,
                                       float wirelength,
                                       float outline_penalty,
                                       float boundary_penalty,
                                       float macro_blockage_penalty) const
{
  float cost = 0.0;
  cost += alpha_ * area / norm_area_;
  if (norm_wirelength_ > 0) {
    cost += beta_ * wirelength / norm_wirelength_;
  }

  if (norm_outline_penalty_ > 0) {
    cost += gamma_ * outline_penalty / norm_outline_penalty_;
  }

  if (norm_boundary_penalty_ > 0) {
    cost += boundary_weight_ * boundary_penalty / norm_boundary_penalty_;
  }

  if (norm_macro_blockage_penalty_ > 0) {
    cost += macro_blockage_weight_ * macro_blockage_penalty
            / norm_macro_blockage_penalty_;
  }

  return cost;
}

void SimulatedAnnealingCore::Initialize()
{
  vector<float> area_list;
  vector<float> wirelength_list;
  vector<float> outline_penalty_list;
  vector<float> boundary_penalty_list;
  vector<float> macro_blockage_penalty_list;
  norm_area_ = 0.0;
  norm_wirelength_ = 0.0;
  norm_outline_penalty_ = 0.0;
  norm_boundary_penalty_ = 0.0;
  norm_macro_blockage_penalty_ = 0.0;
  for (int i = 0; i < perturb_per_step_; i++) {
    Perturb();
    CalculateWirelength();
    CalculateOutlinePenalty();
    CalculateBoundaryPenalty();
    CalculateMacroBlockagePenalty();
    area_list.push_back(area_);
    wirelength_list.push_back(wirelength_);
    outline_penalty_list.push_back(outline_penalty_);
    boundary_penalty_list.push_back(boundary_penalty_);
    macro_blockage_penalty_list.push_back(macro_blockage_penalty_);
    norm_area_ += area_;
    norm_wirelength_ += wirelength_;
    norm_outline_penalty_ += outline_penalty_;
    norm_boundary_penalty_ += boundary_penalty_;
    norm_macro_blockage_penalty_ += macro_blockage_penalty_;
  }

  norm_area_ = norm_area_ / perturb_per_step_;
  norm_wirelength_ = norm_wirelength_ / perturb_per_step_;
  norm_outline_penalty_ = norm_outline_penalty_ / perturb_per_step_;
  norm_boundary_penalty_ = norm_boundary_penalty_ / perturb_per_step_;
  norm_macro_blockage_penalty_
      = norm_macro_blockage_penalty_ / perturb_per_step_;

  vector<float> cost_list;
  for (int i = 0; i < area_list.size(); i++)
    cost_list.push_back(NormCost(area_list[i],
                                 wirelength_list[i],
                                 outline_penalty_list[i],
                                 boundary_penalty_list[i],
                                 macro_blockage_penalty_list[i]));

  float delta_cost = 0.0;
  for (int i = 1; i < cost_list.size(); i++)
    delta_cost += abs(cost_list[i] - cost_list[i - 1]);

  init_T_ = (-1.0) * (delta_cost / (perturb_per_step_ - 1)) / log(init_prob_);
}

void SimulatedAnnealingCore::ShrinkBlocks()
{
  for (int i = 0; i < blocks_.size(); i++) {
    if (blocks_[i].GetNumMacro() == 0) {
      blocks_[i].ShrinkSoftBlock(shrink_factor_, shrink_factor_);
    }
  }
}

bool SimulatedAnnealingCore::FitFloorplan()
{
  float width_factor = outline_width_ / width_;
  float height_factor = outline_height_ / height_;
  vector<Block> pre_blocks = blocks_;
  for (int i = 0; i < blocks_.size(); i++) {
    blocks_[i].ShrinkSoftBlock(width_factor, height_factor);
  }

  PackFloorplan();

  for (int i = 0; i < blocks_.size(); i++) {
    if (blocks_[i].GetNumMacro() > 0) {
      float ux = blocks_[i].GetX() + blocks_[i].GetWidth();
      float uy = blocks_[i].GetY() + blocks_[i].GetHeight();
      float x = blocks_[i].GetX() + 1.0 * blocks_[i].GetWidth();
      float y = blocks_[i].GetY() + 1.0 * blocks_[i].GetHeight();
      blocks_[i].ShrinkSoftBlock(1.0 / width_factor, 1.0 / height_factor);
      if (ux >= outline_width_ * 0.99) {
        x -= blocks_[i].GetWidth();
        blocks_[i].SpecifyX(x);
      }

      if (uy >= outline_height_ * 0.99) {
        y -= blocks_[i].GetHeight();
        blocks_[i].SpecifyY(y);
      }
    } else {
      blocks_[i].ShrinkSoftBlock(1.0 / width_factor, 1.0 / height_factor);
    }
  }

  vector<pair<float, float>> macro_block_x_list;
  vector<pair<float, float>> macro_block_y_list;

  for (int i = 0; i < blocks_.size(); i++) {
    if (blocks_[i].GetNumMacro() > 0) {
      float lx = blocks_[i].GetX();
      float ux = lx + blocks_[i].GetWidth();
      float ly = blocks_[i].GetY();
      float uy = ly + blocks_[i].GetHeight();
      macro_block_x_list.push_back(pair<float, float>(lx, ux));
      macro_block_y_list.push_back(pair<float, float>(ly, uy));
    }
  }

  float overlap = 0.0;
  for (int i = 0; i < macro_block_x_list.size(); i++) {
    for (int j = i + 1; j < macro_block_x_list.size(); j++) {
      float x1 = max(macro_block_x_list[i].first, macro_block_x_list[j].first);
      float x2
          = min(macro_block_x_list[i].second, macro_block_x_list[j].second);
      float y1 = max(macro_block_y_list[i].first, macro_block_y_list[j].first);
      float y2
          = min(macro_block_y_list[i].second, macro_block_y_list[j].second);
      float x = 0.0;
      float y = 0.0;
      overlap += max(x2 - x1, x) * max(y2 - y1, y);
    }
  }

  // We don't allow the overlap between macros and macro blockages
  CalculateMacroBlockagePenalty();
  if (overlap + macro_blockage_penalty_ > 0.0) {
    blocks_ = pre_blocks;
    PackFloorplan();
    return false;
  }

  return true;
}

void SimulatedAnnealingCore::UpdateWeight(float avg_area,
                                          float avg_wirelength,
                                          float avg_outline_penalty,
                                          float avg_boundary_penalty,
                                          float avg_macro_blockage_penalty)
{
  avg_area = avg_area / (perturb_per_step_ * norm_area_);

  if (norm_wirelength_ != 0.0) {
    avg_wirelength = avg_wirelength / (perturb_per_step_ * norm_wirelength_);
  }

  if (norm_outline_penalty_ != 0.0) {
    avg_outline_penalty
        = avg_outline_penalty / (perturb_per_step_ * norm_outline_penalty_);
  }

  if (norm_boundary_penalty_ != 0.0) {
    avg_boundary_penalty
        = avg_boundary_penalty / (perturb_per_step_ * norm_boundary_penalty_);
  }

  if (norm_macro_blockage_penalty_ != 0.0) {
    avg_macro_blockage_penalty
        = avg_macro_blockage_penalty
          / (perturb_per_step_ * norm_macro_blockage_penalty_);
  }

  float sum_cost = avg_area + avg_wirelength + avg_outline_penalty
                   + avg_boundary_penalty + avg_macro_blockage_penalty;
  float new_alpha = avg_area / sum_cost;
  float new_beta = avg_wirelength / sum_cost;
  float new_gamma = avg_outline_penalty / sum_cost;
  float new_boundary_weight = avg_boundary_penalty / sum_cost;
  float new_macro_blockage_weight = avg_macro_blockage_penalty / sum_cost;

  new_alpha = alpha_base_ * (1 - new_alpha * learning_rate_);
  new_beta = beta_base_ * (1 - new_beta * learning_rate_);
  new_gamma = gamma_base_ * (1 - new_gamma * learning_rate_);
  new_boundary_weight
      = boundary_weight_base_ * (1 - new_boundary_weight * learning_rate_);
  new_macro_blockage_weight
      = macro_blockage_weight_base_
        * (1 - new_macro_blockage_weight * learning_rate_);

  float new_weight = new_alpha + new_beta + new_gamma + new_boundary_weight
                     + new_macro_blockage_weight;
  alpha_ = new_alpha / new_weight;
  beta_ = new_beta / new_weight;
  gamma_ = new_gamma / new_weight;
  boundary_weight_ = new_boundary_weight / new_weight;
  macro_blockage_weight_ = new_macro_blockage_weight / new_weight;
}

void SimulatedAnnealingCore::FastSA()
{
  float pre_cost = NormCost(area_,
                            wirelength_,
                            outline_penalty_,
                            boundary_penalty_,
                            macro_blockage_penalty_);
  float cost = pre_cost;
  float delta_cost = 0.0;

  float best_cost = cost;
  // vector<Block> best_blocks = blocks_;
  // vector<int> best_pos_seq = pos_seq_;
  // vector<int> best_neg_seq = neg_seq_;

  float avg_area = 0.0;
  float avg_wirelength = 0.0;
  float avg_outline_penalty = 0.0;
  float avg_boundary_penalty = 0.0;
  float avg_macro_blockage_penalty = 0.0;

  int step = 1;
  float rej_num = 0.0;
  float T = init_T_;
  float rej_threshold = rej_ratio_ * perturb_per_step_;

  int max_num_restart = 2;
  int num_restart = 0;
  int max_num_shrink = int(1.0 / shrink_freq_);
  int num_shrink = 0;
  int modulo_base = int(max_num_step_ * shrink_freq_);

  while (step < max_num_step_) {
    avg_area = 0.0;
    avg_wirelength = 0.0;
    avg_outline_penalty = 0.0;
    avg_boundary_penalty = 0.0;
    avg_macro_blockage_penalty = 0.0;

    rej_num = 0.0;
    float accept_rate = 0.0;
    float avg_delta_cost = 0.0;
    for (int i = 0; i < perturb_per_step_; i++) {
      Perturb();
      CalculateWirelength();
      CalculateOutlinePenalty();
      CalculateBoundaryPenalty();
      CalculateMacroBlockagePenalty();
      cost = NormCost(area_,
                      wirelength_,
                      outline_penalty_,
                      boundary_penalty_,
                      macro_blockage_penalty_);

      delta_cost = cost - pre_cost;
      float num = distribution_(generator_);
      float prob = (delta_cost > 0.0) ? exp((-1) * delta_cost / T) : 1;
      avg_delta_cost += abs(delta_cost);
      if (delta_cost < 0 || num < prob) {
        pre_cost = cost;
        accept_rate += 1.0;
        if (cost < best_cost) {
          best_cost = cost;
          // best_blocks = blocks_;
          // best_pos_seq = pos_seq_;
          // best_neg_seq = neg_seq_;

          if ((num_shrink <= max_num_shrink) && (step % modulo_base == 0)
              && (IsFeasible() == false)) {
            num_shrink += 1;
            ShrinkBlocks();
            PackFloorplan();
            CalculateWirelength();
            CalculateOutlinePenalty();
            CalculateBoundaryPenalty();
            CalculateMacroBlockagePenalty();
            pre_cost = NormCost(area_,
                                wirelength_,
                                outline_penalty_,
                                boundary_penalty_,
                                macro_blockage_penalty_);
            best_cost = pre_cost;
          }
        }
      } else {
        rej_num += 1.0;
        Restore();
      }

      avg_area += area_;
      avg_wirelength += wirelength_;
      avg_outline_penalty += outline_penalty_;
      avg_boundary_penalty += boundary_penalty_;
      avg_macro_blockage_penalty += macro_blockage_penalty_;
    }

    UpdateWeight(avg_area,
                 avg_wirelength,
                 avg_outline_penalty,
                 avg_boundary_penalty,
                 avg_macro_blockage_penalty);
    // cout << "step:  " << step << "  T:  " << T << " cost:  " << cost << endl;

    step++;

    /*
    if(step <= k_)
        T = init_T_ / (step * c_ * avg_delta_cost / perturb_per_step_);
    else
        T = init_T_ / (step * avg_delta_cost / perturb_per_step_);
    */

    T = T * cooling_rate_;

    if (step == max_num_step_) {
      // blocks_ = best_blocks;
      // pos_seq_ = best_pos_seq;
      // neg_seq_ = best_neg_seq;

      PackFloorplan();
      CalculateWirelength();
      CalculateOutlinePenalty();
      CalculateBoundaryPenalty();
      CalculateMacroBlockagePenalty();
      if (IsFeasible() == false) {
        if (num_restart < max_num_restart) {
          // if(FitFloorplan() == false && num_restart < max_num_restart) {
          // step = int(max_num_step_  * 0.5);
          step = 1;
          T = init_T_;
          num_restart += 1;
        }
      }
    }
  }

  CalculateWirelength();
  CalculateOutlinePenalty();
  CalculateBoundaryPenalty();
  CalculateMacroBlockagePenalty();
}

void Run(SimulatedAnnealingCore* sa)
{
  sa->FastSA();
}

void ParseNetFile(vector<Net*>& nets,
                  unordered_map<string, pair<float, float>>& terminal_position,
                  const char* net_file)
{
  fstream f;
  string line;
  vector<string> content;
  f.open(net_file, ios::in);
  while (getline(f, line))
    content.push_back(line);

  f.close();

  unordered_map<string, pair<float, float>>::iterator terminal_iter;
  int i = 0;
  while (i < content.size()) {
    vector<string> words = Split(content[i]);
    if (words.size() > 2 && words[0] == string("source:")) {
      string source = words[1];
      terminal_iter = terminal_position.find(source);
      bool terminal_flag = true;
      if (terminal_iter == terminal_position.end())
        terminal_flag = false;

      int j = 2;
      while (j < words.size()) {
        vector<string> blocks;
        vector<string> terminals;
        if (terminal_flag == true)
          terminals.push_back(source);
        else
          blocks.push_back(source);

        string sink = words[j++];
        terminal_iter = terminal_position.find(sink);
        if (terminal_iter == terminal_position.end())
          blocks.push_back(sink);
        else
          terminals.push_back(sink);

        int weight = stoi(words[j++]);
        Net* net = new Net(weight, blocks, terminals);
        nets.push_back(net);
      }

      i++;
    } else {
      i++;
    }
  }
}

void ParseRegionFile(vector<Region*>& regions, const char* region_file)
{
  fstream f;
  string line;
  vector<string> content;
  f.open(region_file, ios::in);

  // Check wether the file exists
  if (!(f.good()))
    return;

  while (getline(f, line))
    content.push_back(line);

  f.close();

  for (int i = 0; i < content.size(); i++) {
    vector<string> words = Split(content[i]);
    float lx = stof(words[1]);
    float ly = stof(words[2]);
    float ux = stof(words[3]);
    float uy = stof(words[4]);
    Region* region = new Region(lx, ly, ux, uy);
    regions.push_back(region);
  }
}

vector<Block> Floorplan(const vector<shape_engine::Cluster*>& clusters,
                        Logger* logger,
                        float outline_width,
                        float outline_height,
                        const char* net_file,
                        const char* region_file,
                        int num_level,
                        int num_worker,
                        float heat_rate,
                        float alpha,
                        float beta,
                        float gamma,
                        float boundary_weight,
                        float macro_blockage_weight,
                        float resize_prob,
                        float pos_swap_prob,
                        float neg_swap_prob,
                        float double_swap_prob,
                        float init_prob,
                        float rej_ratio,
                        int max_num_step,
                        int k,
                        float c,
                        int perturb_per_step,
                        float learning_rate,
                        float shrink_factor,
                        float shrink_freq,
                        unsigned seed)
{
  logger->info(RMP, 2001, "Block_Placement Starts");

  vector<Block> blocks;
  for (int i = 0; i < clusters.size(); i++) {
    string name = clusters[i]->GetName();
    float area = clusters[i]->GetArea();
    int num_macro = clusters[i]->GetNumMacro();
    vector<pair<float, float>> aspect_ratio = clusters[i]->GetAspectRatio();
    blocks.push_back(Block(name, area, num_macro, aspect_ratio));
  }

  unordered_map<string, pair<float, float>> terminal_position;
  string word = string("LL");
  terminal_position[word] = pair<float, float>(0.0, outline_height / 6.0);
  word = string("RL");
  terminal_position[word]
      = pair<float, float>(outline_width, outline_height / 6.0);
  word = string("BL");
  terminal_position[word] = pair<float, float>(outline_width / 6.0, 0.0);
  word = string("TL");
  terminal_position[word]
      = pair<float, float>(outline_width / 6.0, outline_height);
  word = string("LU");
  terminal_position[word] = pair<float, float>(0.0, outline_height * 5.0 / 6.0);
  word = string("RU");
  terminal_position[word]
      = pair<float, float>(outline_width, outline_height * 5.0 / 6.0);
  word = string("BU");
  terminal_position[word] = pair<float, float>(outline_width * 5.0 / 6.0, 0.0);
  word = string("TU");
  terminal_position[word]
      = pair<float, float>(outline_width * 5.0 / 6.0, outline_height);
  word = string("LM");
  terminal_position[word] = pair<float, float>(0.0, outline_height / 2.0);
  word = string("RM");
  terminal_position[word]
      = pair<float, float>(outline_width, outline_height / 2.0);
  word = string("BM");
  terminal_position[word] = pair<float, float>(outline_width / 2.0, 0.0);
  word = string("TM");
  terminal_position[word]
      = pair<float, float>(outline_width / 2.0, outline_height);

  vector<Net*> nets;
  ParseNetFile(nets, terminal_position, net_file);

  vector<Region*> regions;
  ParseRegionFile(regions, region_file);

  int num_seed = num_level * num_worker + 10;  // 10 is for guardband
  int seed_id = 0;
  unsigned* seed_list = new unsigned[num_seed];
  std::mt19937 rand_generator(seed);
  for (int i = 0; i < num_seed; i++)
    seed_list[i] = (unsigned) rand_generator();

  SimulatedAnnealingCore* sa = new SimulatedAnnealingCore(outline_width,
                                                          outline_height,
                                                          blocks,
                                                          nets,
                                                          regions,
                                                          terminal_position,
                                                          0.99,
                                                          alpha,
                                                          beta,
                                                          gamma,
                                                          boundary_weight,
                                                          macro_blockage_weight,
                                                          resize_prob,
                                                          pos_swap_prob,
                                                          neg_swap_prob,
                                                          double_swap_prob,
                                                          init_prob,
                                                          rej_ratio,
                                                          max_num_step,
                                                          k,
                                                          c,
                                                          perturb_per_step,
                                                          learning_rate,
                                                          shrink_factor,
                                                          shrink_freq,
                                                          seed_list[seed_id++]);

  sa->Initialize();
  logger->info(RMP, 2002, "Block_Placement  Finish Initialization");

  SimulatedAnnealingCore* best_sa = nullptr;
  float best_cost = FLT_MAX;
  float norm_area = sa->GetNormArea();
  float norm_wirelength = sa->GetNormWirelength();
  float norm_outline_penalty = sa->GetNormOutlinePenalty();
  float norm_boundary_penalty = sa->GetNormBoundaryPenalty();
  float norm_macro_blockage_penalty = sa->GetNormMacroBlockagePenalty();
  float init_T = sa->GetInitT();

  logger->info(RMP, 2003, "Block_Placement Init_T: {}", init_T);

  blocks = sa->GetBlocks();
  vector<int> pos_seq = sa->GetPosSeq();
  vector<int> neg_seq = sa->GetNegSeq();

  float heat_count = 1.0;

  for (int i = 0; i < num_level; i++) {
    init_T = init_T * heat_count;
    heat_count = heat_count * heat_rate;
    vector<SimulatedAnnealingCore*> sa_vec;
    vector<thread> threads;
    float cooling_rate_step = (0.99 - 0.01) / num_worker;
    for (int j = 0; j < num_worker; j++) {
      float cooling_rate = 0.99;
      if (num_worker >= 2) {
        cooling_rate = 0.99 - j * (0.99 - 0.01) / (num_worker - 1);
      }

      cout << "init_T:  " << init_T << endl;
      cout << "thread:  " << j << "  cooling_rate:   " << cooling_rate << endl;
      SimulatedAnnealingCore* sa
          = new SimulatedAnnealingCore(outline_width,
                                       outline_height,
                                       blocks,
                                       nets,
                                       regions,
                                       terminal_position,
                                       cooling_rate,
                                       alpha,
                                       beta,
                                       gamma,
                                       boundary_weight,
                                       macro_blockage_weight,
                                       resize_prob,
                                       pos_swap_prob,
                                       neg_swap_prob,
                                       double_swap_prob,
                                       init_prob,
                                       rej_ratio,
                                       max_num_step,
                                       k,
                                       c,
                                       perturb_per_step,
                                       learning_rate,
                                       shrink_factor,
                                       shrink_freq,
                                       seed_list[seed_id++]);

      sa->Initialize(init_T,
                     norm_area,
                     norm_wirelength,
                     norm_outline_penalty,
                     norm_boundary_penalty,
                     norm_macro_blockage_penalty);

      sa->SpecifySeq(pos_seq, neg_seq);
      sa_vec.push_back(sa);
      threads.push_back(thread(Run, sa));
    }

    for (auto& th : threads)
      th.join();

    for (int j = 0; j < num_worker; j++) {
      if (best_cost > sa_vec[j]->GetCost()) {
        best_cost = sa_vec[j]->GetCost();
        best_sa = sa_vec[j];
      }

      cout << "thread:  " << j << "  cost:  " << sa_vec[j]->GetCost() << endl;
    }

    blocks = best_sa->GetBlocks();
    pos_seq = best_sa->GetPosSeq();
    neg_seq = best_sa->GetNegSeq();

    // verify the result
    string output_info = "level:  ";
    output_info += to_string(i) + "   ";
    output_info += "cost:  ";
    output_info += to_string(best_sa->GetCost()) + "   ";
    output_info += "area:   ";
    output_info += to_string(best_sa->GetArea()) + "   ";
    output_info += "wirelength:  ";
    output_info += to_string(best_sa->GetWirelength()) + "   ";
    output_info += "outline_penalty:  ";
    output_info += to_string(best_sa->GetOutlinePenalty()) + "   ";
    output_info += "boundary_penalty:  ";
    output_info += to_string(best_sa->GetBoundaryPenalty()) + "   ";
    output_info += "macro_blockage_penalty:  ";
    output_info += to_string(best_sa->GetMacroBlockagePenalty());

    logger->info(RMP, 2004 + i, "Block_Placement {}", output_info);

    for (int j = 0; j < num_worker; j++) {
      if (best_cost < sa_vec[j]->GetCost()) {
        delete sa_vec[j];
      }
    }
  }

  blocks = best_sa->GetBlocks();
  logger->info(RMP,
               2004 + num_level,
               "Block_Placement Floorplan width: {}",
               best_sa->GetWidth());
  logger->info(RMP,
               2005 + num_level,
               "Block_Placement Floorplan height: {}",
               best_sa->GetHeight());
  logger->info(RMP,
               2006 + num_level,
               "Block_Placement Outline width: {}",
               outline_width);
  logger->info(RMP,
               2007 + num_level,
               "Block_Placement Outline height: {}",
               outline_height);

  if (!(best_sa->IsFeasible()))
    logger->info(
        RMP, 2008 + num_level, "Block_Placement No Feasible Floorplan");

  return blocks;
}

}  // namespace block_placement
