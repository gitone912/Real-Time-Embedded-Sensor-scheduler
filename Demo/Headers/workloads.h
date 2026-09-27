/*
 * FreeRTOS Kernel <DEVELOPMENT BRANCH>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#ifndef WORKLOADS_H
#define WORKLOADS_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief Unified FreeRTOS Task Wrapper for both HRT and SRT execution.
 * * This wrapper dynamically queries the timeline scheduler to determine
 * its own task type (HRT or SRT) and invokes the correct completion
 * signal once the deterministic payload finishes executing.
 * * @param[in] pvParameters Pointer to the statically allocated scaling factor.
 */
void vGenericTaskWrapper( void *pvParameters );

/**
 * @brief Simulates a deterministic CPU-bound workload.
 * * @param[in] ulWorkloadScalingFactor Number of iterations to perform.
 */
void vSimulateEmbeddedWorkload( uint32_t ulWorkloadScalingFactor );

#endif /* WORKLOADS_H */