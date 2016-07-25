/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2013, OpenCV Foundation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "layers_common.hpp"

namespace cv
{
namespace dnn
{

void getCaffeConvParams(LayerParams &params, Size &kernel, Size &pad, Size &stride)
{
    if (params.has("kernel_h") && params.has("kernel_w"))
    {
        kernel.height = params.get<int>("kernel_h");
        kernel.width = params.get<int>("kernel_w");
    }
    else if (params.has("kernel_size"))
    {
        kernel.height = kernel.width = params.get<int>("kernel_size");
    }
    else
    {
        CV_Error(Error::StsBadArg, "kernel_size (or kernel_h and kernel_w) not specified");
    }
    CV_Assert(kernel.height > 0 && kernel.width > 0);

    if (params.has("pad_h") && params.has("pad_w"))
    {
        pad.height = params.get<int>("pad_h");
        pad.width = params.get<int>("pad_w");
    }
    else
    {
        pad.height = pad.width = params.get<int>("pad", 0);
    }
    CV_Assert(pad.height >= 0 && pad.width >= 0);

    if (params.has("stride_h") && params.has("stride_w"))
    {
        stride.height = params.get<int>("stride_h");
        stride.width = params.get<int>("stride_w");
    }
    else
    {
        stride.height = stride.width = params.get<int>("stride", 1);
    }
    CV_Assert(stride.height > 0 && stride.width > 0);
}

}
}
