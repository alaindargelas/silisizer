// Silisizer, Operator sizer
// Copyright (c) 2024, Silimate Inc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
#include "Silisizer.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <list>
#include <regex>

#include "sta/Liberty.hh"
#include "sta/Network.hh"
#include "sta/PathEnd.hh"
#include "sta/PortDirection.hh"
#include "sta/Sta.hh"

namespace SILISIZER {

std::string replaceAll(std::string_view str, std::string_view from,
                       std::string_view to) {
  size_t start_pos = 0;
  std::string result(str);
  while ((start_pos = result.find(from, start_pos)) != std::string::npos) {
    result.replace(start_pos, from.length(), to);
    start_pos += to.length();  // Handles case where 'to' is a substr of 'from'
  }
  return result;
}

int Silisizer::silisize(int max_timer_iterations, int nb_concurrent_paths,
                        int nb_initial_concurrent_changes,
                        int nb_high_effort_concurrent_changes) {
  sta::Network* network = this->network();
  uint32_t timing_group_count = nb_concurrent_paths;
  uint32_t end_point_count = nb_concurrent_paths;
  uint32_t concurrent_replace_count = nb_initial_concurrent_changes;
  bool debug = 0;
  std::ofstream transforms("preqorsor/data/resized_cells.csv");
  if (transforms.good()) {
    transforms << "Scope" << "," << "Instance" << "," << "From cell" << ","
               << "To cell" << std::endl;
  }
  int loopCount = 0;
  double previous_wns = 0.0f;
  bool max_effort = false;
  double previousWNSDelta = 0.0f;
  while (1) {
    std::cout << "  Timer is called..." << std::endl;
    sta::PathEndSeq ends = sta_->findPathEnds(
        /*exception from*/ nullptr, /*exception through*/ nullptr,
        /*exception to*/ nullptr, /*unconstrained*/ false, /*corner*/ nullptr,
        sta::MinMaxAll::all(),
        /*group_count*/ timing_group_count, /*endpoint_count*/ end_point_count,
        /*unique_pins*/ true,
        /* min_slack */ -1.0e+30, /*max_slack*/ 0.0,
        /*sort_by_slack*/ false,
        /*groups->size() ? groups :*/ nullptr,
        /*setup*/ true, /*hold*/ false,
        /*recovery*/ false, /*removal*/ false,
        /*clk_gating_setup*/ false, /*clk_gating_hold*/ false);
    bool moreOptNeeded = !ends.empty();
    if (!moreOptNeeded) {
      std::cout << "Final WNS: 0ps" << std::endl;
      std::cout << "Timing optimization done!" << std::endl;
      break;
    }
    std::unordered_map<sta::Instance*, double> offendingInstCount;
    if (debug) std::cout << "Retuned nb paths: " << ends.size() << std::endl;
    double wns = 0.0f;
    bool fixableWnsPath = false;
    sta::PathEnd* the_wnsPath = nullptr;
    for (sta::PathEnd* pathend : ends) {
      sta::Path* path = pathend->path();
      sta::Pin* pin = path->pin(this);
      if (debug)
        std::cout << "End Violation at: " << network->name(pin) << std::endl;
      sta::PathRef p(path);
      float slack = pathend->slack(this);
      if (slack >= 0.0) continue;
      bool is_wnsPath = false;
      if (slack < wns) {
        fixableWnsPath = false;
        is_wnsPath = true;
        wns = slack;
        the_wnsPath = pathend;
      }
      while (!p.isNull()) {
        pin = p.pin(this);
        sta::PathRef prev_path;
        sta::TimingArc* prev_arc = nullptr;
        p.prevPath(this, prev_path, prev_arc);
        sta::Delay delay = 0.0f;
        if (prev_arc) delay = prev_arc->intrinsicDelay();
        sta::Instance* inst = network->instance(pin);
        sta::Cell* cell = network->cell(inst);
        // If the instance does not have a cell, skip
        if (!cell) {
          p.prevPath(this, p);
          continue;
        }
        sta::LibertyCell* libcell = network->libertyCell(cell);
        // If it's not a liberty cell (module), skip
        if (!libcell) {
          p.prevPath(this, p);
          continue;
        }
        std::string libcellname = libcell->name();
        // If it's not a speed0 cell, skip
        if (libcellname.find("_sp0_") == std::string::npos) {
          p.prevPath(this, p);
          if (debug) std::cout << "Speed1 cell: " << libcellname << std::endl;
          continue;
        }
        if (offendingInstCount.find(inst) == offendingInstCount.end()) {
          offendingInstCount.emplace(inst, delay);
        } else {
          offendingInstCount.find(inst)->second += delay;
        }
        if (is_wnsPath) fixableWnsPath = true;
        if (debug)
          std::cout << " From: " << network->name(inst) << " / "
                    << network->name(pin) << std::endl;
        p.prevPath(this, p);
      }
    }
    if (debug)
      std::cout << "offendingInstCount: " << offendingInstCount.size()
                << std::endl;
    if (offendingInstCount.empty()) {
      if (wns == 0.0f) {
        std::cout << "Final WNS: " << "0ps" << std::endl;
        std::cout << "Timing optimization done!" << std::endl;
      } else {
        std::cout << "Final WNS: " << -(wns * 1e12) << "ps" << std::endl;
        std::cout << "Timing optimization partially done!" << std::endl;
      }
      break;
    }
    if (!fixableWnsPath) {
      std::cout << "Final WNS: " << -(wns * 1e12) << "ps" << std::endl;
      std::cout << "WARNING: WNS Path does not contain any resizable cells!\n";
      if (the_wnsPath) {
        sta::Path* path = the_wnsPath->path();
        sta::PathRef p(path);
        std::set<std::string> reported;
        while (!p.isNull()) {
          sta::Pin* pin = p.pin(this);
          sta::Instance* inst = network->instance(pin);
          sta::Cell* cell = network->cell(inst);
          std::string libcellname;
          if (cell) {
            sta::LibertyCell* libcell = network->libertyCell(cell);
            if (libcell) {
              libcellname = libcell->name();
            }
          }
          std::string cellname = network->name(inst);
          cellname = replaceAll(cellname, "\\[", "[");
          cellname = replaceAll(cellname, "\\]", "]");
          cellname = replaceAll(cellname, "\\\\", "\\");
          if (reported.find(cellname) == reported.end()) {
            if (!cellname.empty())
              std::cout << "WNS Path: " << cellname << " (" << libcellname
                        << ")" << std::endl;
            reported.insert(cellname);
          }
          p.prevPath(this, p);
        }
      }
      std::cout << "Timing optimization partially done!" << std::endl;
      break;
    }
    std::list<std::pair<sta::Instance*, double>> offenders;
    for (auto pair : offendingInstCount) {
      if (offenders.empty()) {
        offenders.push_back(std::pair(pair.first, pair.second));
      } else {
        for (std::list<std::pair<sta::Instance*, double>>::iterator itr =
                 offenders.begin();
             itr != offenders.end(); itr++) {
          if ((*itr).second < pair.second) {
            offenders.insert(++itr, std::pair(pair.first, pair.second));
            if (offenders.size() > concurrent_replace_count) {
              offenders.pop_back();
            }
            break;
          }
        }
      }
    }
    if (debug) std::cout << "offenders: " << offenders.size() << std::endl;
    if (offenders.empty()) {
      std::cout << "Final WNS: " << "0ps" << std::endl;
      std::cout << "Timing optimization done!" << std::endl;
      break;
    }

    for (auto offender_pair : offenders) {
      sta::Instance* offender = offender_pair.first;
      sta::Cell* cell = network->cell(offender);
      sta::LibertyLibrary* library = network->libertyLibrary(offender);
      sta::LibertyCell* libcell = network->libertyCell(cell);
      std::string from_cell_name = libcell->name();
      std::string to_cell_name =
          std::regex_replace(from_cell_name, std::regex("_sp0_"), "_sp1_");
      // if (debug)
      std::string fullname;
      sta::Instance* parent = network->parent(offender);
      std::string parentcellname = network->cellName(parent);
      while (parent) {
        std::string parentName = network->name(parent);
        if (!parentName.empty()) fullname += parentName + ".";
        parent = network->parent(parent);
      }
      std::string cellname = network->name(offender);
      cellname = replaceAll(cellname, "\\[", "[");
      cellname = replaceAll(cellname, "\\]", "]");
      cellname = replaceAll(cellname, "\\\\", "\\");
      std::cout << "  Resizing instance " << fullname + cellname
                << " of type: " << from_cell_name
                << " to type: " << to_cell_name << std::endl;
      sta::LibertyCell* to_cell =
          library->findLibertyCell(to_cell_name.c_str());

      if (!to_cell) {
        std::cout << "WARNING: Missing cell model: " << to_cell_name
                  << std::endl;
        std::cout << "Final WNS: " << -(wns * 1e12) << "ps" << std::endl;
        std::cout << "Timing optimization partially done!" << std::endl;
        transforms.close();
        return 0;
      }
      Sta::sta()->replaceCell(offender, to_cell);
      if (transforms.good()) {
        transforms << "\"" << parentcellname << "\"" << "," << cellname << ","
                   << from_cell_name << "," << to_cell_name << std::endl;
      }
    }
    loopCount++;
    if ((!max_effort) && (loopCount == 10)) {
      concurrent_replace_count = nb_high_effort_concurrent_changes / 4;
      if (concurrent_replace_count < 1) {
        concurrent_replace_count = 1;
      }
      std::cout << "Increasing concurrent resizings to: "
                << concurrent_replace_count << std::endl;
    }
    double deltaWNS = fabs((fabs(wns) * 1e12) - (fabs(previous_wns) * 1e12));
    if (loopCount > 1) {
      std::cout << "Delta WNS: " << deltaWNS << "ps" << std::endl;
    }
    if ((!max_effort) &&
        (((loopCount == (max_timer_iterations / 2)) || (deltaWNS < 0.1)))) {
      timing_group_count *= 2;
      end_point_count *= 2;
      concurrent_replace_count = nb_high_effort_concurrent_changes;
      std::cout << "Analysing " << end_point_count << " paths" << std::endl;
      max_effort = true;
    } else if ((max_effort) && (deltaWNS == 0.0) && (previousWNSDelta == 0.0)) {
      std::cout << "WARNING: Cannot meet timing constraints!" << std::endl;
      std::cout << "Final WNS: " << -(wns * 1e12) << "ps" << std::endl;
      std::cout << "Timing optimization partially done!" << std::endl;
      transforms.close();
      return 0;
    } else if ((max_effort) && (deltaWNS != 0.0) && (deltaWNS < 10.0)) {
      concurrent_replace_count *= 2;
      if (concurrent_replace_count > 10000) concurrent_replace_count = 10000;
      timing_group_count *= 2;
      if (timing_group_count > 2000) timing_group_count = 2000;
      end_point_count *= 2;
      if (end_point_count > 2000) {
        end_point_count = 2000;
      }
      std::cout << "Analysing " << end_point_count << " paths" << std::endl;
    }
    std::cout << "Iteration " << loopCount << " out of " << max_timer_iterations
              << std::endl;
    if (loopCount >= max_timer_iterations) {
      std::cout << "WARNING: Cannot meet timing constraints!" << std::endl;
      std::cout << "Final WNS: " << -(wns * 1e12) << "ps" << std::endl;
      std::cout << "Timing optimization partially done!" << std::endl;
      transforms.close();
      return 0;
    }
    std::cout << "Current WNS: " << -(wns * 1e12) << "ps" << std::endl;
    previous_wns = wns;
    previousWNSDelta = deltaWNS;
  }
  transforms.close();
  return 0;
}

}  // namespace SILISIZER
