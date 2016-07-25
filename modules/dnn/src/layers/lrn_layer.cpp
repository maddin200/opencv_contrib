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

#include "../precomp.hpp"
#include "layers_common.hpp"
#include "lrn_layer.hpp"
#include "opencl_kernels_dnn.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/dnn/shape_utils.hpp>
#include <algorithm>

namespace cv
{
namespace dnn
{

LRNLayerImpl::LRNLayerImpl()
{
    size = 5;
    alpha = 1;
    beta = 0.75;
    type = CHANNEL_NRM;
}

void LRNLayerImpl::allocate(const std::vector<Blob*> &inputs, std::vector<Blob> &outputs)
{
    CV_Assert(inputs.size() == 1 && inputs[0]->dims() == 4);
    useOpenCL = cv::ocl::useOpenCL();

    if (type == SPATIAL_NRM && !useOpenCL)
        buf.create(inputs[0]->shape().slice(2), inputs[0]->type(), Blob::ALLOC_MAT);
    if (type == CHANNEL_NRM && useOpenCL)
        buf.create(inputs[0]->shape().slice(2), inputs[0]->type(), Blob::ALLOC_UMAT);

    outputs.resize(1);
    outputs[0].create(inputs[0]->shape(), inputs[0]->type());
}

void LRNLayerImpl::forward(std::vector<Blob*> &inputs, std::vector<Blob> &outputs)
{
    Blob &src = *inputs[0];
    Blob &dst = outputs[0];

    switch (type)
    {
    case CHANNEL_NRM:
        channelNoramlization(src, dst);
        break;
    case SPATIAL_NRM:
        spatialNormalization(src, dst);
        break;
    default:
        CV_Error(Error::StsNotImplemented, "Unimplemented mode of LRN layer");
        break;
    }
}

template<typename XMat>
static XMat getPlane(XMat &m, int n, int cn)
{
    return reshaped(slice(m, n, cn), BlobShape::like(m).slice(2));
}

void LRNLayerImpl::channelNoramlization(Blob &src, Blob &dst)
{
    if (!useOpenCL)
        channelNoramlization_<Mat>(src, dst);
    else
    {
        //channelNoramlization_ocl(src.getRefConst<UMat>(), dst.getRef<UMat>()); //consumes a lot of memory
        channelNoramlization_<UMat>(src, dst);
    }
}

template<typename XMat>
void LRNLayerImpl::channelNoramlization_(Blob &srcBlob, Blob &dstBlob)
{
    int num = srcBlob.num();
    int channels = srcBlob.channels();
    int ksize = (size - 1) / 2;

    XMat srcMat = srcBlob.getRefConst<XMat>();
    XMat dstMat = dstBlob.getRef<XMat>();

    for (int n = 0; n < num; n++)
    {
        XMat accum = getPlane(dstMat, n, channels-1); //trick for memory saving
        accum.setTo(0);

        for (int cn = 0; cn < std::min(ksize, channels); cn++)
            cv::accumulateSquare(getPlane(srcMat, n, cn), accum);

        for (int cn = 0; cn < channels; cn++)
        {
            if (cn + ksize < channels)
            {
                cv::accumulateSquare(getPlane(srcMat, n, cn + ksize), accum);
            }

            if (cn - ksize - 1 >= 0)
            {
                //subtractSquare
                XMat left = getPlane(srcMat, n, cn - ksize - 1);
                cv::pow(left, 2, left);
                cv::subtract(accum, left, accum);
            }

            XMat dst = getPlane(dstMat, n, cn);
            accum.convertTo(dst, dst.type(), alpha/size, 1);
            cv::pow(dst, beta, dst);
            cv::divide(getPlane(srcMat, n, cn), dst, dst);
        }
    }
}

bool LRNLayerImpl::channelNoramlization_ocl(const UMat &src, UMat &dst)
{
    if (src.offset != 0 || dst.offset != 0) //TODO: add offset
        return false;

    String buildOpts = String("-DT=") + ocl::typeToStr(src.type());

    ocl::Kernel kerScale("LRNFillScale", ocl::dnn::lrn_oclsrc, buildOpts);
    if (kerScale.empty())
        return false;

    ocl::Kernel kerOutput("LRNComputeOutput", ocl::dnn::lrn_oclsrc, buildOpts);
    if (kerOutput.empty())
        return false;

    Shape shape = Shape::like(src);
    int ksize = (size - 1) / 2;
    size_t wgSize = ocl::Device::getDefault().maxWorkGroupSize();
    UMat &scaleBuf = buf.umatRef();

    size_t nthreads = (size_t)(shape.total() / shape[1]);
    kerScale.args((int)nthreads,
                  ocl::KernelArg::PtrReadOnly(src), shape[0], shape[1], shape[2], shape[3],
                  size, (float)(alpha/size), (float)ksize, ocl::KernelArg::PtrWriteOnly(scaleBuf));
    if (!kerScale.run(1, &nthreads, &wgSize, true))
        return false;

    nthreads = (size_t)shape.total();
    kerOutput.args((int)nthreads,
                   ocl::KernelArg::PtrReadOnly(src), ocl::KernelArg::PtrReadOnly(scaleBuf),
                   -beta, ocl::KernelArg::PtrWriteOnly(dst) );
    if (!kerOutput.run(1, &nthreads, &wgSize, true))
        return false;

    return true;
}

void LRNLayerImpl::spatialNormalization(Blob &src, Blob &dst)
{
    if (!useOpenCL)
        spatialNormalization_<Mat>(src, dst);
    else
        spatialNormalization_<UMat>(src, dst);
}

template<typename XMat>
void LRNLayerImpl::spatialNormalization_(Blob &srcBlob, Blob &dstBlob)
{
    int num = srcBlob.num();
    int channels = srcBlob.channels();

    XMat srcMat = srcBlob.getRefConst<XMat>();
    XMat dstMat = dstBlob.getRef<XMat>();

    for (int n = 0; n < num; n++)
    {
        for (int cn = 0; cn < channels; cn++)
        {
            XMat src = getPlane(srcMat, n, cn);
            XMat dst = getPlane(dstMat, n, cn);

            if (MatTraits<XMat>::IS_UMAT)
            {
                cv::sqrBoxFilter(src, dst, dst.depth(), Size(size, size), Point(-1, -1), false, BORDER_CONSTANT | BORDER_ISOLATED);
            }
            else
            {
                //TODO: fix cv::boxFilter with BORDER_ISOLATED flag in CPU mode
                Mat bufMat = buf.getRef<Mat>();
                src.copyTo(bufMat);
                cv::sqrBoxFilter(bufMat, dst, dst.depth(), Size(size, size), Point(-1, -1), false, BORDER_CONSTANT);
            }

            dst.convertTo(dst, dst.type(), alpha/(size*size), 1);
            cv::pow(dst, beta, dst);
            cv::divide(src, dst, dst);
        }
    }
}

Ptr<Layer> createLRNLayerFromCaffe(LayerParams &params)
{
    LRNLayerImpl *l = new LRNLayerImpl();

    String nrmType = params.get<String>("norm_region", "ACROSS_CHANNELS");
    if (nrmType == "ACROSS_CHANNELS")
        l->type = LRNLayer::CHANNEL_NRM;
    else if (nrmType == "WITHIN_CHANNEL")
        l->type = LRNLayer::SPATIAL_NRM;
    else
        CV_Error(Error::StsBadArg, "Unknown region type \"" + nrmType + "\"");

    int size = params.get<int>("local_size", 5);
    if (size % 2 != 1 || size <= 0)
        CV_Error(Error::StsBadArg, "LRN layer supports only positive odd values for local_size");
    l->size = size;

    l->alpha = params.get<double>("alpha", 1);
    l->beta = params.get<double>("beta", 0.75);

    return Ptr<Layer>(l);
}

}
}
