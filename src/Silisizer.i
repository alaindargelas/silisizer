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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

%module silisizer

%inline %{

// Silisizer: resize operator-level cells to resolve timing violations
extern int silisize(
                    int max_timer_iterations = 200, // max tries
                    int nb_concurrent_paths = 10,  // # of paths to analyze
                    int nb_initial_concurrent_changes = 3, // init swap rate
                    int nb_high_effort_concurrent_changes = 20, // max swap rate
                    double arc_weight_exponent = 1.0 // nb_occur * (arc_timing ^ arc_weight_exponent)
                    );

%}
