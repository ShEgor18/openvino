// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <random>
#include <string>

// clang-format off
#ifdef ${BACKEND_NAME}_FLOAT_TOLERANCE_BITS
#define DEFAULT_FLOAT_TOLERANCE_BITS ${BACKEND_NAME}_FLOAT_TOLERANCE_BITS
#endif

#ifdef ${BACKEND_NAME}_DOUBLE_TOLERANCE_BITS
#define DEFAULT_DOUBLE_TOLERANCE_BITS ${BACKEND_NAME}_DOUBLE_TOLERANCE_BITS
#endif
// clang-format on

#include "gtest/gtest.h"
#include "ngraph/ngraph.hpp"
#include "util/engine/test_engines.hpp"
#include "util/test_case.hpp"
#include "util/test_control.hpp"

using namespace std;
using namespace ngraph;

static string s_manifest = "${MANIFEST}";
using TestEngine = test::ENGINE_CLASS_NAME(${BACKEND_NAME});

NGRAPH_TEST(${BACKEND_NAME}, minimum)
{
    Shape shape{2, 2, 2};
    auto A = make_shared<op::Parameter>(element::f32, shape);
    auto B = make_shared<op::Parameter>(element::f32, shape);
    auto f = make_shared<Function>(make_shared<op::v1::Minimum>(A, B), ParameterVector{A, B});

    std::vector<float> a{1, 8, -8, 17, -0.5, 0.5, 2, 1};
    std::vector<float> b{1, 2, 4, 8, 0, 0, 1, 1.5};

    auto test_case = test::TestCase<TestEngine>(f);
    test_case.add_multiple_inputs<float>({a, b});
    test_case.add_expected_output<float>(shape, {1, 2, -8, 8, -.5, 0, 1, 1});
    test_case.run();
}

NGRAPH_TEST(${BACKEND_NAME}, minimum_int32)
{
    Shape shape{2, 2, 2};
    auto A = make_shared<op::Parameter>(element::i32, shape);
    auto B = make_shared<op::Parameter>(element::i32, shape);
    auto f = make_shared<Function>(make_shared<op::v1::Minimum>(A, B), ParameterVector{A, B});

    std::vector<int32_t> a{1, 8, -8, 17, -5, 67635216, 2, 1};
    std::vector<int32_t> b{1, 2, 4, 8, 0, 18448, 1, 6};

    auto test_case = test::TestCase<TestEngine>(f);
    test_case.add_multiple_inputs<int32_t>({a, b});
    test_case.add_expected_output<int32_t>(shape, {1, 2, -8, 8, -5, 18448, 1, 1});
    test_case.run();
}

NGRAPH_TEST(${BACKEND_NAME}, minimum_int64)
{
    Shape shape{2, 2, 2};
    auto A = make_shared<op::Parameter>(element::i64, shape);
    auto B = make_shared<op::Parameter>(element::i64, shape);
    auto f = make_shared<Function>(make_shared<op::v1::Minimum>(A, B), ParameterVector{A, B});

    std::vector<int64_t> a{1, 8, -8, 17, -5, 67635216, 2, 17179887632};
    std::vector<int64_t> b{1, 2, 4, 8, 0, 18448, 1, 280592};

    auto test_case = test::TestCase<TestEngine>(f);
    test_case.add_multiple_inputs<int64_t>({a, b});
    test_case.add_expected_output<int64_t>(shape, {1, 2, -8, 8, -5, 18448, 1, 280592});
    test_case.run();
}

NGRAPH_TEST(${BACKEND_NAME}, minimum_u16)
{
    Shape shape{3};
    auto A = make_shared<op::Parameter>(element::u16, shape);
    auto B = make_shared<op::Parameter>(element::u16, shape);
    auto f = make_shared<Function>(make_shared<op::v1::Minimum>(A, B), ParameterVector{A, B});

    std::vector<uint16_t> a{3, 2, 1};
    std::vector<uint16_t> b{1, 4, 4};

    auto test_case = test::TestCase<TestEngine>(f);
    test_case.add_multiple_inputs<uint16_t>({a, b});
    test_case.add_expected_output<uint16_t>(shape, {1, 2, 1});
    test_case.run();
}
