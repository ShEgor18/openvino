# Copyright (C) 2018-2021 Intel Corporation
# SPDX-License-Identifier: Apache-2.0

import numpy as np
import pytest

from common.tf_layer_test_class import CommonTFLayerTest


class TestMul(CommonTFLayerTest):
    def create_mul_placeholder_const_net(self, x_shape, y_shape, ir_version):
        """
            Tensorflow net                  IR net

            Placeholder->Mul       =>       Placeholder->Eltwise or Power or ScaleShift
                         /                               /
            Const-------/                   Const-------/

        """

        #
        #   Create Tensorflow model
        #

        import tensorflow as tf

        tf.compat.v1.reset_default_graph()

        # Create the graph and model
        with tf.compat.v1.Session() as sess:
            tf_x_shape = x_shape.copy()
            tf_y_shape = y_shape.copy()
            # reshaping
            if len(tf_x_shape) >= 3:
                tf_x_shape.append(tf_x_shape.pop(1))
            if len(tf_y_shape) >= 3:
                tf_y_shape.append(tf_y_shape.pop(1))

            x = tf.compat.v1.placeholder(tf.float32, tf_x_shape, 'Input')
            constant_value = np.random.randint(-255, 255, tf_y_shape).astype(np.float32)
            if (constant_value == 1).all():
                # Avoid elimination of the layer from IR
                constant_value = constant_value + 1
            y = tf.constant(constant_value)

            mul = tf.multiply(x, y, name="Operation")
            mul_shape = mul.shape.as_list()

            tf.compat.v1.global_variables_initializer()
            tf_net = sess.graph_def

        #
        #   Create reference IR net
        #   Please, specify 'type': 'Input' for input node
        #   Moreover, do not forget to validate ALL layer attributes!!!
        #

        if len(mul_shape) >= 3:
            # Permute mul_shape to (N,C,...) format
            order = [0, len(mul_shape) - 1] + list(range(1, len(mul_shape) - 1))
            mul_shape = [mul_shape[i] for i in order]

        y_shape_to_compare = tf_y_shape.copy()
        while len(y_shape_to_compare) < len(x_shape):
            # Expand shape of constant with 1
            y_shape_to_compare = [1] + y_shape_to_compare
            constant_value = np.expand_dims(constant_value, axis=0)

        if len(y_shape_to_compare) >= 3:
            # Permute constant_value to (N,C,...) format for correct further reshape
            order = [0, len(y_shape_to_compare) - 1] + list(range(1, len(y_shape_to_compare) - 1))
            y_shape_to_compare = [y_shape_to_compare[i] for i in order]
            constant_value = np.transpose(constant_value, order)

        ref_net = None

        return tf_net, ref_net

    # TODO: implement tests for 2 Consts + Mul

    test_data_1D = [
        # Power
        dict(x_shape=[1], y_shape=[1]),
        # Eltwise
        pytest.param(dict(x_shape=[3], y_shape=[3]), marks=pytest.mark.xfail(reason="*-19180"))
    ]

    @pytest.mark.parametrize("params", test_data_1D)
    @pytest.mark.nightly
    def test_mul_placeholder_const_1D(self, params, ie_device, precision, ir_version, temp_dir):
        self._test(*self.create_mul_placeholder_const_net(**params, ir_version=ir_version),
                   ie_device, precision, ir_version, temp_dir=temp_dir)

    test_data_2D = [
        # Power
        dict(x_shape=[1, 1], y_shape=[1, 1]),
        # ScaleShift
        dict(x_shape=[1, 3], y_shape=[1, 3]),
        # Eltwise
        pytest.param(dict(x_shape=[3, 1], y_shape=[3, 1]), marks=pytest.mark.xfail(reason="*-19180")),
        # Eltwise
        dict(x_shape=[2, 3], y_shape=[2, 3])
    ]

    @pytest.mark.parametrize("params", test_data_2D)
    @pytest.mark.nightly
    def test_mul_placeholder_const_2D(self, params, ie_device, precision, ir_version, temp_dir):
        self._test(*self.create_mul_placeholder_const_net(**params, ir_version=ir_version),
                   ie_device, precision, ir_version, temp_dir=temp_dir)

    test_data_3D = [
        # Power
        dict(x_shape=[1, 1, 1], y_shape=[1, 1, 1]),
        # ScaleShift
        pytest.param(dict(x_shape=[1, 3, 1], y_shape=[1, 3, 1]), marks=pytest.mark.xfail(reason="*-19053")),
        # Eltwise
        pytest.param(dict(x_shape=[1, 1, 3], y_shape=[1, 1, 3]),
                     marks=[pytest.mark.xfail(reason="*-19053"), pytest.mark.xfail(reason="*-18830")]),
        # Eltwise
        pytest.param(dict(x_shape=[1, 3, 224], y_shape=[1, 3, 224]), marks=pytest.mark.xfail(reason="*-19053"))
    ]

    @pytest.mark.parametrize("params", test_data_3D)
    @pytest.mark.nightly
    def test_mul_placeholder_const_3D(self, params, ie_device, precision, ir_version, temp_dir):
        self._test(*self.create_mul_placeholder_const_net(**params, ir_version=ir_version),
                   ie_device, precision, ir_version, temp_dir=temp_dir)

    test_data_4D = [
        # Power
        dict(x_shape=[1, 1, 1, 1], y_shape=[1, 1, 1, 1]),
        # ScaleShift
        dict(x_shape=[1, 3, 1, 1], y_shape=[1, 3, 1, 1]),
        # Eltwise
        pytest.param(dict(x_shape=[1, 1, 1, 3], y_shape=[1, 1, 1, 3]), marks=pytest.mark.xfail(reason="*-19180")),
        # Eltwise
        dict(x_shape=[1, 3, 222, 224], y_shape=[1, 3, 222, 224])
    ]

    @pytest.mark.parametrize("params", test_data_4D)
    @pytest.mark.nightly
    def test_mul_placeholder_const_4D(self, params, ie_device, precision, ir_version, temp_dir):
        self._test(*self.create_mul_placeholder_const_net(**params, ir_version=ir_version),
                   ie_device, precision, ir_version, temp_dir=temp_dir)

    test_data_5D = [
        # Power
        dict(x_shape=[1, 1, 1, 1, 1], y_shape=[1, 1, 1, 1, 1]),
        # ScaleShift
        dict(x_shape=[1, 3, 1, 1, 1], y_shape=[1, 3, 1, 1, 1]),
        # Eltwise
        pytest.param(dict(x_shape=[1, 1, 1, 1, 3], y_shape=[1, 1, 1, 1, 3]),
                     marks=pytest.mark.xfail(reason="*-19180")),
        # Eltwise
        dict(x_shape=[1, 3, 50, 100, 224], y_shape=[1, 3, 50, 100, 224])
    ]

    # TODO mark as precommit (after successfully passing in nightly)
    @pytest.mark.parametrize("params", test_data_5D)
    @pytest.mark.nightly
    def test_mul_placeholder_const_5D(self, params, ie_device, precision, ir_version, temp_dir):
        self._test(*self.create_mul_placeholder_const_net(**params, ir_version=ir_version),
                   ie_device, precision, ir_version, temp_dir=temp_dir)

    ###############################################################################################
    #                                                                                             #
    #                                       Broadcast cases                                       #
    #                                                                                             #
    ###############################################################################################

    test_data_broadcast_1D = [  # Power
        dict(x_shape=[3], y_shape=[1])
    ]

    @pytest.mark.parametrize("params", test_data_broadcast_1D)
    @pytest.mark.nightly
    def test_mul_placeholder_const_broadcast_1D(self, params, ie_device, precision, ir_version, temp_dir):
        self._test(*self.create_mul_placeholder_const_net(**params, ir_version=ir_version),
                   ie_device, precision, ir_version, temp_dir=temp_dir)

    test_data_broadcast_2D = [
        # Power
        dict(x_shape=[1, 1], y_shape=[1]),
        # Power
        dict(x_shape=[1, 3], y_shape=[1]),
        # ScaleShift
        dict(x_shape=[1, 3], y_shape=[3]),
        # Eltwise
        dict(x_shape=[3, 1], y_shape=[3]),
        # Eltwise
        pytest.param(dict(x_shape=[3, 1], y_shape=[1, 3, 1, 1]), marks=pytest.mark.xfail(reason="*-19051"))
    ]

    @pytest.mark.parametrize("params", test_data_broadcast_2D)
    @pytest.mark.nightly
    def test_mul_placeholder_const_broadcast_2D(self, params, ie_device, precision, ir_version, temp_dir):
        self._test(*self.create_mul_placeholder_const_net(**params, ir_version=ir_version),
                   ie_device, precision, ir_version, temp_dir=temp_dir)

    test_data_broadcast_3D = [
        # Power
        dict(x_shape=[1, 1, 1], y_shape=[1]),
        # Power
        pytest.param(dict(x_shape=[1, 3, 1], y_shape=[1]), marks=pytest.mark.xfail(reason="*-19053")),
        # ScaleShift
        pytest.param(dict(x_shape=[1, 3, 1], y_shape=[3]), marks=pytest.mark.xfail(reason="*-19053")),
        # Eltwise
        pytest.param(dict(x_shape=[1, 3, 1], y_shape=[3, 1]), marks=pytest.mark.xfail(reason="*-19053")),
        # Eltwise
        pytest.param(dict(x_shape=[1, 1, 1], y_shape=[3, 1]), marks=pytest.mark.xfail(reason="*-19053")),
        # Eltwise
        pytest.param(dict(x_shape=[3, 1, 224], y_shape=[1, 3, 224]), marks=pytest.mark.xfail(reason="*-19053")),
        # Eltwise
        pytest.param(dict(x_shape=[2, 3, 1], y_shape=[1, 3, 2]), marks=pytest.mark.xfail(reason="*-19053")),
    ]

    # TODO mark as precommit (after successfully passing in nightly)
    @pytest.mark.parametrize("params", test_data_broadcast_3D)
    @pytest.mark.nightly
    def test_mul_placeholder_const_broadcast_3D(self, params, ie_device, precision, ir_version, temp_dir):
        self._test(*self.create_mul_placeholder_const_net(**params, ir_version=ir_version),
                   ie_device, precision, ir_version, temp_dir=temp_dir)

    test_data_broadcast_4D = [
        # Power
        dict(x_shape=[1, 1, 1, 1], y_shape=[1]),
        # Power
        dict(x_shape=[1, 3, 1, 1], y_shape=[1]),
        # ScaleShift
        dict(x_shape=[1, 3, 1, 1], y_shape=[3]),
        # ScaleShift
        dict(x_shape=[1, 3, 100, 224], y_shape=[3]),
        # Eltwise
        dict(x_shape=[1, 1, 1, 3], y_shape=[3]),
        # Eltwise
        dict(x_shape=[1, 3, 1, 1], y_shape=[3, 1]),
        # Eltwise
        dict(x_shape=[1, 3, 1, 2], y_shape=[3, 1, 2]),
        # Eltwise
        dict(x_shape=[1, 3, 1, 2], y_shape=[1, 3, 2]),
        # Eltwise
        dict(x_shape=[1, 3, 100, 224], y_shape=[1, 1, 1, 224]),
        # Eltwise
        dict(x_shape=[2, 3, 1, 2], y_shape=[1, 3, 2, 1])
    ]

    @pytest.mark.parametrize("params", test_data_broadcast_4D)
    @pytest.mark.nightly
    @pytest.mark.precommit
    def test_mul_placeholder_const_broadcast_4D(self, params, ie_device, precision, ir_version, temp_dir):
        self._test(*self.create_mul_placeholder_const_net(**params, ir_version=ir_version),
                   ie_device, precision, ir_version, temp_dir=temp_dir)

    test_data_broadcast_5D = [
        # Power
        dict(x_shape=[1, 1, 1, 1, 1], y_shape=[1]),
        # Power
        dict(x_shape=[1, 3, 1, 1, 1], y_shape=[1, 1]),
        # ScaleShift
        dict(x_shape=[1, 3, 1, 1, 1], y_shape=[3]),
        # Eltwise
        dict(x_shape=[1, 1, 1, 1, 3], y_shape=[3]),
        # Eltwise
        dict(x_shape=[1, 3, 1, 1, 1], y_shape=[3, 1]),
        # Eltwise
        dict(x_shape=[1, 3, 1, 1, 2], y_shape=[1, 3, 2]),
        # Eltwise
        dict(x_shape=[1, 3, 5, 1, 2], y_shape=[5, 3, 2, 1]),
        # Eltwise
        dict(x_shape=[1, 3, 50, 100, 224], y_shape=[1, 1, 1, 1, 224]),
        # Eltwise
        dict(x_shape=[2, 3, 1, 2, 1], y_shape=[1, 3, 2, 1, 1])
    ]

    @pytest.mark.parametrize("params", test_data_broadcast_5D)
    @pytest.mark.nightly
    def test_mul_placeholder_const_broadcast_5D(self, params, ie_device, precision, ir_version, temp_dir):
        self._test(*self.create_mul_placeholder_const_net(**params, ir_version=ir_version),
                   ie_device, precision, ir_version, temp_dir=temp_dir)
