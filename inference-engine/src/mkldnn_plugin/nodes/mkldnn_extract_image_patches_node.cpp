// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "base.hpp"

#include <cstring>
#include <string>
#include <cmath>

#include <ngraph/opsets/opset3.hpp>
#include "ie_parallel.hpp"
#include "mkldnn_extract_image_patches_node.h"
#include "list.hpp"
#include <cpu/x64/jit_generator.hpp>
#include "caseless.hpp"

using namespace MKLDNNPlugin;
using namespace InferenceEngine;

using details::CaselessEq;

using namespace dnnl::impl::cpu;
using namespace dnnl::impl::cpu::x64;
using namespace dnnl::impl::utils;
using namespace Xbyak;

#define GET_OFF(field) offsetof(jit_extract_image_patches_args, field)

template <cpu_isa_t isa>
struct jit_extract_image_patches_kernel : public jit_uni_extract_image_patches_kernel, public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_extract_image_patches_kernel)

    explicit jit_extract_image_patches_kernel(jit_extract_image_patches_params jpp) : jit_uni_extract_image_patches_kernel(jpp), jit_generator() {}

    void create_ker() override {
        jit_generator::create_kernel();
        ker_ = (decltype(ker_))jit_ker();
    }

    void generate() override {
        this->preamble();

        mov(reg_num_pads, ptr[reg_params + GET_OFF(h_lo_pad)]);
        mov(reg_h_hi_pad, ptr[reg_params + GET_OFF(h_hi_pad)]);
        mov(reg_w_lo_pad, ptr[reg_params + GET_OFF(w_lo_pad)]);
        mov(reg_w_hi_pad, ptr[reg_params + GET_OFF(w_hi_pad)]);
        mov(reg_src, ptr[reg_params + GET_OFF(src)]);
        mov(reg_dst, ptr[reg_params + GET_OFF(dst)]);

        mov(reg_src_incr, jpp.SH * jpp.IW * jpp.dtype_size);
        mov(reg_aux64, reg_w_hi_pad);
        mul_by_const(reg_aux64, reg_aux64_2, jpp.SW * jpp.dtype_size);
        sub(reg_src_incr, reg_aux64);

        mov(reg_aux64, reg_w_lo_pad);
        mul_by_const(reg_aux64, reg_aux64_2, jpp.SW * jpp.dtype_size);
        add(reg_src_incr, reg_aux64);
        add(reg_src, reg_aux64);

        mov(reg_ow_work_amount, reg_w_hi_pad);
        sub(reg_ow_work_amount, reg_w_lo_pad);

        uni_vpxor(vmm_zero, vmm_zero, vmm_zero);
        if (mayiuse_gather) {
            mov(reg_aux64, gather_index_table);
            uni_vmovups(vmm_gather_index, ptr[reg_aux64]);
        }
        loop();

        this->postamble();

        if (mayiuse_gather)
            prepare_table();
    }

private:
    using Vmm = typename conditional3<isa == x64::sse41, Xbyak::Xmm, isa == x64::avx2, Xbyak::Ymm, Xbyak::Zmm>::type;
    using reg64_t = const Xbyak::Reg64;
    using reg32_t = const Xbyak::Reg32;
    bool mayiuse_gather = (mayiuse(x64::avx2) || mayiuse(x64::avx512_common)) && (jpp.dtype_size == 4);
    uint32_t vlen = cpu_isa_traits<isa>::vlen;
    reg64_t reg_src = r8;
    reg64_t reg_dst = r9;
    reg64_t reg_oh_count = r10;
    reg64_t reg_ow_count = r11;
    reg64_t reg_num_pads = r12;
    reg64_t reg_src_incr = r13;
    reg64_t reg_aux64 = rax;
    reg64_t reg_w_hi_pad = r14;
    reg64_t reg_w_lo_pad = r15;
    reg64_t reg_h_hi_pad = rbp;
    reg64_t reg_aux64_2 = rbx;
    reg64_t reg_ow_work_amount = rsi;
    reg64_t reg_params = abi_param1;

    Vmm vmm = Vmm(0);
    Xmm xmm = Xmm(0);
    Vmm vmm_zero = Vmm(1); // reserved for pad
    Xbyak::Xmm xmm_aux = Xbyak::Xmm(2);
    Vmm vmm_gather_index = Vmm(3);
    Vmm vmm_gather_mask = Vmm(4);
    Opmask k_mask = Xbyak::Opmask(1);
    Xbyak::Label gather_index_table;

    inline void load_scalar(Vmm vmm_arg, const Xbyak::Address &op) {
        Xbyak::Xmm xmm_src = Xmm(vmm_arg.getIdx());
        switch (jpp.dtype_size) {
            case 4: uni_vmovss(vmm_arg, op); break;
            case 2: uni_vpinsrw(xmm_src, xmm_src, op, 0x0); break;
            case 1: uni_vpinsrb(xmm_src, xmm_src, op, 0x0); break;
            default: IE_THROW() << "The data type of size '" << jpp.dtype_size << "' is not supported.";
        }
    }
    inline void store_scalar(const Xbyak::Address &op, Vmm vmm_arg) {
        Xbyak::Xmm xmm_dst = Xmm(vmm_arg.getIdx());
        switch (jpp.dtype_size) {
            case 4: uni_vmovss(op, vmm_arg); break;
            case 2: uni_vpextrw(op, xmm_dst, 0x0); break;
            case 1: uni_vpextrb(op, xmm_dst, 0x0); break;
            default: IE_THROW() << "The data type of size '" << jpp.dtype_size << "' is not supported.";
        }
    }

    inline void pad_with_zeros(reg64_t &reg_num_pads_arg, reg64_t &reg_dst_arg) {
        Xbyak::Label main, tail, exit;
        L(main);
        {
            cmp(reg_num_pads_arg, jpp.block_size);
            jl(tail);
            uni_vmovups(ptr[reg_dst_arg], vmm_zero);
            add(reg_dst_arg, jpp.dtype_size * jpp.block_size);
            sub(reg_num_pads_arg, jpp.block_size);
            jmp(main);
        }
        L(tail);
        {
            cmp(reg_num_pads_arg, 0);
            jle(exit);
            store_scalar(ptr[reg_dst_arg], vmm_zero);
            add(reg_dst_arg, jpp.dtype_size);
            dec(reg_num_pads_arg);
            jmp(tail);
        }
        L(exit);
    }

    inline void custom_uni_vgatherdps(const Vmm &vmm_arg, reg64_t &mem_base, const Vmm &mem_offset, Vmm &vmm_mask) {
        switch (isa) {
            case x64::avx2:
                uni_vpcmpeqd(vmm_mask, vmm_mask, vmm_mask);
                vgatherdps(vmm_arg, ptr[mem_base + mem_offset], vmm_mask);
                break;
            case x64::avx512_common:
                kxnord(k_mask, k_mask, k_mask);
                vgatherdps(vmm_arg | k_mask, ptr[mem_base + mem_offset]);
                break;
            case x64::sse41:
                emulate_gather(vmm_arg, mem_base);
                break;
            default: IE_THROW() << "Got unsupported instruction set.";
        }
    }

    inline void gather_src2vmm(const Vmm &vmm_arg, reg64_t &mem_base) {
        switch (jpp.dtype_size) {
            case 4: custom_uni_vgatherdps(vmm, mem_base, vmm_gather_index, vmm_gather_mask); break;
            case 2:
            case 1: emulate_gather(vmm_arg, mem_base); break;
            default: IE_THROW() << "The data type of size '" << jpp.dtype_size << "' is not supported.";
        }
    }

    inline void emulate_gather(const Xbyak::Xmm &xmm_arg, reg64_t &mem_base, int xmm_offset = 0) {
        const int xmm_size = 16; // bytes
        const int xmm_block_size = xmm_size / jpp.dtype_size;
        const int offset = xmm_offset * jpp.SW * jpp.dtype_size * xmm_block_size;
        for (int i = 0; i < xmm_block_size; i++) {
            Xbyak::Address addr = ptr[mem_base + i * jpp.SW * jpp.dtype_size + offset];
            switch (jpp.dtype_size) {
                case 4: uni_vpinsrd(xmm_arg, xmm_arg, addr, i); break;
                case 2: uni_vpinsrw(xmm_arg, xmm_arg, addr, i); break;
                case 1: uni_vpinsrb(xmm_arg, xmm_arg, addr, i); break;
                default: IE_THROW() << "The data type of size '" << jpp.dtype_size << "' is not supported.";
            }
        }
    }
    inline void emulate_gather(const Xbyak::Ymm &ymm_arg, reg64_t &mem_base) {
        Xbyak::Xmm low_xmm = Xbyak::Xmm(ymm_arg.getIdx());
        emulate_gather(low_xmm, mem_base, 0);
        emulate_gather(xmm_aux, mem_base, 1);
        vinserti128(ymm_arg, ymm_arg, xmm_aux, 1);
    }

    inline void emulate_gather(const Xbyak::Zmm &zmm_arg, reg64_t &mem_base) {
        Xbyak::Xmm low_xmm = Xbyak::Xmm(zmm_arg.getIdx());
        emulate_gather(low_xmm, mem_base, 0);
        for (int i = 1; i < 4; i++) {
            emulate_gather(xmm_aux, mem_base, i);
            vinserti64x2(zmm_arg, zmm_arg, xmm_aux, i);
        }
    }

    void loop() {
        mov(reg_oh_count, reg_h_hi_pad);
        // reg_num_pads contains h_lo_pad at this point
        sub(reg_oh_count, reg_num_pads);

        Xbyak::Label ih_loop, ih_tail, ih_exit;
        Xbyak::Label iw_loop, iw_tail, iw_exit;
        if (jpp.need_padding) {
            mul_by_const(reg_num_pads, reg_aux64, jpp.OW);
            pad_with_zeros(reg_num_pads, reg_dst);
        }
        L(ih_loop);
        {
            cmp(reg_oh_count, 0);
            jle(ih_exit, T_NEAR);
            if (jpp.need_padding) {
                mov(reg_num_pads, reg_w_lo_pad);
                pad_with_zeros(reg_num_pads, reg_dst);
            }
            mov(reg_ow_count, reg_ow_work_amount);
            L(iw_loop);
            {
                cmp(reg_ow_count, jpp.block_size);
                jle(iw_tail, T_NEAR);
                gather_src2vmm(vmm, reg_src);
                add(reg_src, jpp.SW * jpp.dtype_size * jpp.block_size);
                uni_vmovups(ptr[reg_dst], vmm);
                add(reg_dst, jpp.dtype_size * jpp.block_size);
                sub(reg_ow_count, jpp.block_size);
                jmp(iw_loop);
            }
            L(iw_tail);
            {
                cmp(reg_ow_count, 0);
                jle(iw_exit, T_NEAR);
                load_scalar(vmm, ptr[reg_src]);
                store_scalar(ptr[reg_dst], vmm);
                dec(reg_ow_count);
                add(reg_src, jpp.SW * jpp.dtype_size);
                add(reg_dst, jpp.dtype_size);
                jmp(iw_tail);
            }
            L(iw_exit);
            if (jpp.need_padding) {
                mov(reg_num_pads, jpp.OW);
                sub(reg_num_pads, reg_w_hi_pad);
                pad_with_zeros(reg_num_pads, reg_dst);
            }
            dec(reg_oh_count);
            add(reg_src, reg_src_incr);
            jmp(ih_loop, T_NEAR);
        }
        L(ih_exit);
        if (jpp.need_padding) {
            mov(reg_num_pads, jpp.OH);
            sub(reg_num_pads, reg_h_hi_pad);
            mul_by_const(reg_num_pads, reg_aux64, jpp.OW);
            pad_with_zeros(reg_num_pads, reg_dst);
        }
    }

    void prepare_table() {
        align(64);
        L(gather_index_table);
        for (int32_t i = 0; i < vlen / sizeof(int32_t); i++)
            dd(i * jpp.SW * jpp.dtype_size);
    }
};

bool MKLDNNExtractImagePatchesNode::isSupportedOperation(const std::shared_ptr<ngraph::Node>& op, std::string& errorMessage) noexcept {
    try {
        const auto extImgPatcher = std::dynamic_pointer_cast<const ngraph::opset3::ExtractImagePatches>(op);
        if (!extImgPatcher) {
            errorMessage = "Only opset3 ExtractImagePatches operation is supported";
            return false;
        }
        const auto padValue = extImgPatcher->get_auto_pad();
        if (!one_of(padValue, ngraph::op::PadType::VALID, ngraph::op::PadType::SAME_LOWER, ngraph::op::PadType::SAME_UPPER)) {
            errorMessage = "Does not support pad type: " + ngraph::as_string(padValue);
            return false;
        }
        if (!everyone_is(2, extImgPatcher->get_sizes().size(), extImgPatcher->get_strides().size(), extImgPatcher->get_rates().size())) {
            errorMessage = "Doesn't support 'sizes', 'strides', 'rates', attributes with rank != 2";
            return false;
        }
    } catch (...) {
        return false;
    }
    return true;
}

MKLDNNExtractImagePatchesNode::MKLDNNExtractImagePatchesNode(const std::shared_ptr<ngraph::Node>& op, const mkldnn::engine& eng,
        MKLDNNWeightsSharing::Ptr &cache) : MKLDNNNode(op, eng, cache) {
    std::string errorMessage;
    if (!isSupportedOperation(op, errorMessage)) {
        IE_THROW(NotImplemented) << errorMessage;
    }

    errorPrefix = "ExtractImagePatches layer with name '" + op->get_friendly_name() + "' ";
    const auto extImgPatcher = std::dynamic_pointer_cast<const ngraph::opset3::ExtractImagePatches>(op);

    if (op->get_input_size() != 1 || op->get_output_size() != 1)
        IE_THROW() << errorPrefix << "has incorrect number of input or output edges!"
                   << " Input: " << op->get_input_size() << "; Output: " << op->get_output_size();

    if (op->get_input_shape(0).size() != 4)
        IE_THROW() << errorPrefix << "must have 4D input tensor. Actual: " << op->get_input_shape(0).size();

    if (op->get_output_shape(0).size() != 4)
        IE_THROW() << errorPrefix << "must have 4D output tensor. Actual: " << op->get_output_shape(0).size();

    precision = getOriginalInputPrecisionAtPort(0);
    if (_supported_precisions_sizes.find(precision.size()) == _supported_precisions_sizes.end())
        IE_THROW() << errorPrefix << "has unsupported precision: " << precision.name();

    auto ksizes = extImgPatcher->get_sizes();
    auto strides = extImgPatcher->get_strides();
    auto rates = extImgPatcher->get_rates();
    if (extImgPatcher->get_auto_pad() == ngraph::op::PadType::VALID) {
        _auto_pad = ExtImgPatcherPadType::VALID;
    } else if (extImgPatcher->get_auto_pad() == ngraph::op::PadType::SAME_LOWER) {
        _auto_pad = ExtImgPatcherPadType::SAME_LOWER;
    } else if (extImgPatcher->get_auto_pad() == ngraph::op::PadType::SAME_UPPER) {
        _auto_pad = ExtImgPatcherPadType::SAME_UPPER;
    } else {
        IE_THROW() << errorPrefix << "has unsupported pad type: " << extImgPatcher->get_auto_pad();
    }

    if (ksizes.size() != 2 || strides.size() != 2 || rates.size() != 2)
        IE_THROW() << errorPrefix << "must have the following attributes with shape {2}: sizes, strides, rates.";
    _ksizes.clear();
    _strides.clear();
    _rates.clear();
    for (const auto& x :  ksizes) {
        if (x < 0)
            IE_THROW() << "Kernel sizes must be non-negative, got '" << x << "'.";
        _ksizes.push_back(static_cast<size_t>(x));
    }
    for (const auto& x :  strides) {
        if (x < 0)
            IE_THROW() << "Strides must be non-negative, got '" << x << "'.";
        _strides.push_back(static_cast<size_t>(x));
    }
    for (const auto& x :  rates) {
        if (x < 0)
            IE_THROW() << "Rates must be non-negative, got '" << x << "'.";
        _rates.push_back(static_cast<size_t>(x));
    }

    SizeVector in_dims = op->get_input_shape(0);
    _pad_left = 0;
    _pad_top = 0;
    jit_extract_image_patches_params jpp;
    jpp.need_padding = false;
    if (_auto_pad != ExtImgPatcherPadType::VALID) {
        const size_t iheight = in_dims[2];
        const size_t iwidth = in_dims[3];
        const int64_t ihStep = _ksizes[0] + (_rates[0] - 1) * (_ksizes[0] - 1);
        const int64_t iwStep = _ksizes[1] + (_rates[1] - 1) * (_ksizes[1] - 1);

        int64_t PW = (std::ceil(1.f * iwidth/_strides[1]) - 1) * _strides[1] + iwStep - iwidth;
        int64_t PH = (std::ceil(1.f * iheight/_strides[0]) - 1) * _strides[0] + ihStep - iheight;

        int64_t increment_sign = 0;
        if (_auto_pad == ExtImgPatcherPadType::SAME_LOWER) {
            increment_sign = 1;
        } else if (_auto_pad == ExtImgPatcherPadType::SAME_UPPER) {
            increment_sign = -1;
        }

        if ((PW > 0) && (PW < iwStep)) {
            _pad_left = static_cast<size_t>((PW + increment_sign * (PW % 2)) / 2);
            jpp.need_padding = true;
        }
        if ((PH > 0) && (PH < ihStep)) {
            _pad_top = static_cast<size_t>((PH + increment_sign * (PH % 2)) / 2);
            jpp.need_padding = true;
        }
    }

    jpp.IW = in_dims[3];
    SizeVector out_dims = op->get_output_shape(0);
    jpp.OH = out_dims[2];
    jpp.OW = out_dims[3];
    jpp.KH = _ksizes[0];
    jpp.KW = _ksizes[1];
    jpp.SH = _strides[0];
    jpp.SW = _strides[1];
    jpp.dtype_size = precision.size();
    jpp.block_size = 1;

    if (mayiuse(x64::avx512_common)) {
        jpp.block_size = cpu_isa_traits<x64::avx512_common>::vlen / jpp.dtype_size;
        extract_image_patches_kernel.reset(new jit_extract_image_patches_kernel<x64::avx512_common>(jpp));
    } else if (mayiuse(x64::avx2)) {
        jpp.block_size = cpu_isa_traits<x64::avx2>::vlen / jpp.dtype_size;
        extract_image_patches_kernel.reset(new jit_extract_image_patches_kernel<x64::avx2>(jpp));
    } else if (mayiuse(x64::sse41)) {
        jpp.block_size = cpu_isa_traits<x64::sse41>::vlen / jpp.dtype_size;
        extract_image_patches_kernel.reset(new jit_extract_image_patches_kernel<x64::sse41>(jpp));
    }

    if (extract_image_patches_kernel)
        extract_image_patches_kernel->create_ker();
}

void MKLDNNExtractImagePatchesNode::initSupportedPrimitiveDescriptors() {
    if (!supportedPrimitiveDescriptors.empty())
        return;

    addSupportedPrimDesc({{TensorDescCreatorTypes::ncsp, precision}},
                         {{TensorDescCreatorTypes::ncsp, precision}},
                         impl_desc_type::ref_any);
}

void MKLDNNExtractImagePatchesNode::execute(mkldnn::stream strm) {
    const char *src_data = reinterpret_cast<const char *>(getParentEdgeAt(0)->getMemoryPtr()->GetPtr());
    char *dst_data = reinterpret_cast<char *>(getChildEdgesAtPort(0)[0]->getMemoryPtr()->GetPtr());
    const size_t dtype_size = getParentEdgeAt(0)->getDesc().getPrecision().size();

    const auto& inDims = getParentEdgeAt(0)->getDims().ToSizeVector();
    const size_t IC = inDims[1];
    const size_t IH = inDims[2];
    const size_t IW = inDims[3];

    const auto& outDims = getChildEdgesAtPort(0)[0]->getDims().ToSizeVector();
    const size_t OB = outDims[0];
    const size_t OH = outDims[2];
    const size_t OW = outDims[3];

    const size_t KH = _ksizes[0], KW = _ksizes[1];
    const size_t SH = _strides[0], SW = _strides[1];
    const size_t RH = _rates[0], RW = _rates[1];
    const size_t PT = _pad_top, PL = _pad_left;

    const std::vector<size_t> istrides = getParentEdgeAt(0)->getDesc().getBlockingDesc().getStrides();
    const std::vector<size_t> ostrides = getChildEdgesAtPort(0)[0]->getDesc().getBlockingDesc().getStrides();
    const std::vector<size_t> ostrides_partial = {ostrides[0], KW * IC * ostrides[1], IC * ostrides[1], ostrides[1]};

    if (extract_image_patches_kernel) {
        parallel_for4d(OB, KH, KW, IC, [&](const size_t ob, const size_t kh, const size_t kw, const size_t ic) {
            const int64_t ih_start = kh * RH - PT;
            const int64_t iw_start = kw * RW - PL;
            const size_t ih_lpad = ih_start >= 0 ? 0 : std::ceil(-1.f * ih_start / SH);
            const size_t iw_lpad = iw_start >= 0 ? 0 : std::ceil(-1.f * iw_start / SW);
            const size_t ih_hpad = std::ceil((IH - 1.f * ih_start) / SH) > OH ? OH : std::ceil((IH - 1.f * ih_start) / SH);
            const size_t iw_hpad = std::ceil((IW - 1.f * iw_start) / SW) > OW ? OW : std::ceil((IW - 1.f * iw_start) / SW);

            size_t dst_offset = ob * ostrides_partial[0] + kh * ostrides_partial[1] + kw * ostrides_partial[2] + ic * ostrides_partial[3];
            size_t src_offset = ob * istrides[0] + ic * istrides[1] + ih_start * istrides[2] + iw_start + ih_lpad * SH * IW;

            auto args = jit_extract_image_patches_args();
            args.src = src_data + src_offset * dtype_size;
            args.dst = dst_data + dst_offset * dtype_size;
            args.h_lo_pad = ih_lpad;
            args.h_hi_pad = ih_hpad;
            args.w_lo_pad = iw_lpad;
            args.w_hi_pad = iw_hpad;
            (*extract_image_patches_kernel)(&args);
        });
    } else {
        parallel_for4d(OB, KH, KW, IC, [&](const size_t ob, const size_t kh, const size_t kw, const size_t ic) {
            const int64_t iw_start = kw * RW - PL;
            const int64_t ih_start = kh * RH - PT;
            const size_t ih_lpad = ih_start >= 0 ? 0 : std::ceil(- 1.f * ih_start / SH);
            const size_t iw_lpad = iw_start >= 0 ? 0 : std::ceil(- 1.f * iw_start / SW);

            const size_t ih_hpad = std::ceil((IH - 1.f * ih_start) / SH) > OH ? OH : std::ceil((IH + -1.f * ih_start) / SH);
            const size_t iw_hpad = std::ceil((IW - 1.f * iw_start) / SW) > OW ? OW : std::ceil((IW - 1.f * iw_start) / SW);

            char *my_dst_ptr = dst_data +
                               (ob * ostrides_partial[0] + kh * ostrides_partial[1] + kw * ostrides_partial[2] + ic * ostrides_partial[3]) * dtype_size;
            const char *my_src_ptr = src_data + (ob * istrides[0] + ic * istrides[1] + ih_start * istrides[2] + iw_start) * dtype_size;

            size_t num_bytes_to_set = ih_lpad * OW * dtype_size;
            memset(my_dst_ptr, 0, num_bytes_to_set);
            my_dst_ptr += num_bytes_to_set;

            const char* src_ptr_h_stop = my_src_ptr + ih_hpad * SH * IW * dtype_size;
            for (const char *src_h_ptr = my_src_ptr + ih_lpad * SH * IW * dtype_size;
                 src_h_ptr < src_ptr_h_stop; src_h_ptr += SH * IW * dtype_size) {
                num_bytes_to_set = iw_lpad * dtype_size;
                memset(my_dst_ptr, 0, num_bytes_to_set);
                my_dst_ptr += num_bytes_to_set;

                const char* src_ptr_w_stop = src_h_ptr + iw_hpad * SW * dtype_size;
                for (const char* src_w_ptr = src_h_ptr + iw_lpad * SW * dtype_size;
                     src_w_ptr < src_ptr_w_stop; src_w_ptr += SW * dtype_size) {
                    num_bytes_to_set = dtype_size;
                    memcpy(my_dst_ptr, src_w_ptr, num_bytes_to_set);
                    my_dst_ptr += num_bytes_to_set;
                }
                num_bytes_to_set = (OW - iw_hpad) * dtype_size;
                memset(my_dst_ptr, 0, num_bytes_to_set);
                my_dst_ptr += num_bytes_to_set;
            }
            num_bytes_to_set = (OH - ih_hpad) * OW * dtype_size;
            memset(my_dst_ptr, 0, num_bytes_to_set);
        });
    }
}

const std::set<size_t> MKLDNNExtractImagePatchesNode::_supported_precisions_sizes = {1, 2, 4};

bool MKLDNNExtractImagePatchesNode::created() const {
    return getType() == ExtractImagePatches;
}

REG_MKLDNN_PRIM_FOR(MKLDNNExtractImagePatchesNode, ExtractImagePatches)
