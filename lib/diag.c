/*
 * Microsemi Switchtec(tm) PCIe Management Library
 * Copyright (c) 2021, Microsemi Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/**
 * @file
 * @brief Switchtec diagnostic functions
 */

#define SWITCHTEC_LIB_CORE

#include "switchtec_priv.h"
#include "switchtec/diag.h"
#include "switchtec/endian.h"
#include "switchtec/switchtec.h"
#include "switchtec/utils.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/**
 * @brief Enable cross hair on specified lane
 * @param[in]  dev	Switchtec device handle
 * @param[in]  lane_id	Lane to enable, or SWITCHTEC_DIAG_CROSS_HAIR_ALL_LANES
 *			for all lanes.
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_cross_hair_enable(struct switchtec_dev *dev, int lane_id)
{
	struct switchtec_diag_cross_hair_in in = {
		.sub_cmd = MRPC_CROSS_HAIR_ENABLE,
		.lane_id = lane_id,
		.all_lanes = lane_id == SWITCHTEC_DIAG_CROSS_HAIR_ALL_LANES,
	};

	return switchtec_cmd(dev, MRPC_CROSS_HAIR, &in, sizeof(in), NULL, 0);
}

/**
 * @brief Disable active cross hair
 * @param[in]  dev	Switchtec device handle
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_cross_hair_disable(struct switchtec_dev *dev)
{
	struct switchtec_diag_cross_hair_in in = {
		.sub_cmd = MRPC_CROSS_HAIR_DISABLE,
	};

	return switchtec_cmd(dev, MRPC_CROSS_HAIR, &in, sizeof(in), NULL, 0);
}

/**
 * @brief Disable active cross hair
 * @param[in]  dev		Switchtec device handle
 * @param[in]  start_lane_id	Start lane ID to get
 * @param[in]  num_lanes	Number of lanes to get
 * @param[out] res		Resulting cross hair data
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_cross_hair_get(struct switchtec_dev *dev, int start_lane_id,
		int num_lanes, struct switchtec_diag_cross_hair *res)
{
	struct switchtec_diag_cross_hair_in in = {
		.sub_cmd = MRPC_CROSS_HAIR_GET,
		.lane_id = start_lane_id,
		.num_lanes = num_lanes,
	};
	struct switchtec_diag_cross_hair_get out[num_lanes];
	int i, ret;

	ret = switchtec_cmd(dev, MRPC_CROSS_HAIR, &in, sizeof(in), &out,
			    sizeof(out));
	if (ret)
		return ret;

	for (i = 0; i < num_lanes; i++) {
		memset(&res[i], 0, sizeof(res[i]));
		res[i].state = out[i].state;
		res[i].lane_id = out[i].lane_id;

		if (out[i].state <= SWITCHTEC_DIAG_CROSS_HAIR_WAITING) {
			continue;
		} else if (out[i].state < SWITCHTEC_DIAG_CROSS_HAIR_DONE) {
			res[i].x_pos = out[i].x_pos;
			res[i].y_pos = out[i].y_pos;
		} else if (out[i].state == SWITCHTEC_DIAG_CROSS_HAIR_DONE) {
			res[i].eye_left_lim = out[i].eye_left_lim;
			res[i].eye_right_lim = out[i].eye_right_lim;
			res[i].eye_bot_left_lim = out[i].eye_bot_left_lim;
			res[i].eye_bot_right_lim = out[i].eye_bot_right_lim;
			res[i].eye_top_left_lim = out[i].eye_top_left_lim;
			res[i].eye_top_right_lim = out[i].eye_top_right_lim;
		} else if (out[i].state == SWITCHTEC_DIAG_CROSS_HAIR_ERROR) {
			res[i].x_pos = out[i].x_pos;
			res[i].y_pos = out[i].y_pos;
			res[i].prev_state = out[i].prev_state;
		}
	}

	return 0;
}

static int switchtec_diag_eye_status(int status)
{
	switch (status) {
	case 0: return 0;
	case 2:
		errno = EINVAL;
		return -1;
	case 3:
		errno = EBUSY;
		return -1;
	default:
		errno = EPROTO;
		return -1;
	}
}

static int switchtec_diag_eye_cmd(struct switchtec_dev *dev, void *in,
				  size_t size)
{
	struct switchtec_diag_port_eye_cmd out;
	int ret;

	ret = switchtec_cmd(dev, MRPC_EYE_OBSERVE, in, size, &out,
			    sizeof(out));

	if (ret)
		return ret;

	return switchtec_diag_eye_status(out.status);
}

/**
 * @brief Set the data mode for the next Eye Capture
 * @param[in]  dev	       Switchtec device handle
 * @param[in]  mode	       Mode to use (raw or ratio)
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_eye_set_mode(struct switchtec_dev *dev,
				enum switchtec_diag_eye_data_mode mode)
{
	struct switchtec_diag_port_eye_cmd in = {
		.sub_cmd = MRPC_EYE_OBSERVE_SET_DATA_MODE,
		.data_mode = mode,
	};

	return switchtec_diag_eye_cmd(dev, &in, sizeof(in));
}

/**
 * @brief Start a PCIe Eye Capture
 * @param[in]  dev	       Switchtec device handle
 * @param[in]  lane_mask       Bitmap of the lanes to capture
 * @param[in]  x_range         Time range: start should be between 0 and 63,
 *			       end between start and 63.
 * @param[in]  y_range         Voltage range: start should be between -255 and 255,
 *			       end between start and 255.
 * @param[in]  step_interval   Sampling time in milliseconds for each step
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_eye_start(struct switchtec_dev *dev, int lane_mask[4],
			     struct range *x_range, struct range *y_range,
			     int step_interval)
{
	int err;
	int ret;
	struct switchtec_diag_port_eye_start in = {
		.sub_cmd = MRPC_EYE_OBSERVE_START,
		.lane_mask[0] = lane_mask[0],
		.lane_mask[1] = lane_mask[1],
		.lane_mask[2] = lane_mask[2],
		.lane_mask[3] = lane_mask[3],
		.x_start = x_range->start,
		.y_start = y_range->start,
		.x_end = x_range->end,
		.y_end = y_range->end,
		.x_step = x_range->step,
		.y_step = y_range->step,
		.step_interval = step_interval,
	};

	ret = switchtec_diag_eye_cmd(dev, &in, sizeof(in));

	/* Add delay so hardware has enough time to start */
	err = errno;
	usleep(200000);
	errno = err;

	return ret;
}

static uint64_t hi_lo_to_uint64(uint32_t lo, uint32_t hi)
{
	uint64_t ret;

	ret = le32toh(hi);
	ret <<= 32;
	ret |= le32toh(lo);

	return ret;
}

/**
 * @brief Start a PCIe Eye Capture
 * @param[in]  dev	       Switchtec device handle
 * @param[out] pixels          Resulting pixel data
 * @param[in]  pixel_cnt       Space in pixel array
 * @param[out] lane_id         The lane for the resulting pixels
 *
 * @return number of pixels fetched on success, error code on failure
 *
 * pixel_cnt needs to be greater than 62 in raw mode or 496 in ratio
 * mode, otherwise data will be lost and the number of pixels fetched
 * will be greater than the space in the pixel buffer.
 */
int switchtec_diag_eye_fetch(struct switchtec_dev *dev, double *pixels,
			     size_t pixel_cnt, int *lane_id)
{
	struct switchtec_diag_port_eye_cmd in = {
		.sub_cmd = MRPC_EYE_OBSERVE_FETCH,
	};
	struct switchtec_diag_port_eye_fetch out;
	uint64_t samples, errors;
	int i, ret, data_count;

retry:
	ret = switchtec_cmd(dev, MRPC_EYE_OBSERVE, &in, sizeof(in), &out,
			    sizeof(out));
	if (ret)
		return ret;

	if (out.status == 1) {
		usleep(5000);
		goto retry;
	}

	ret = switchtec_diag_eye_status(out.status);
	if (ret)
		return ret;

	for (i = 0; i < 4; i++) {
		*lane_id = ffs(out.lane_mask[i]);
		if (*lane_id)
			break;
	}

	data_count = out.data_count_lo | ((int)out.data_count_hi << 8);

	for (i = 0; i < data_count && i < pixel_cnt; i++) {
		switch (out.data_mode) {
		case SWITCHTEC_DIAG_EYE_RAW:
			errors = hi_lo_to_uint64(out.raw[i].error_cnt_lo,
						 out.raw[i].error_cnt_hi);
			samples = hi_lo_to_uint64(out.raw[i].sample_cnt_lo,
						  out.raw[i].sample_cnt_hi);
			if (samples)
				pixels[i] = (double)errors / samples;
			else
				pixels[i] = nan("");
			break;
		case SWITCHTEC_DIAG_EYE_RATIO:
			pixels[i] = le32toh(out.ratio[i].ratio) / 65536.;
			break;
		}
	}

	return data_count;
}

/**
 * @brief Cancel in-progress eye capture
 * @param[in]  dev	       Switchtec device handle
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_eye_cancel(struct switchtec_dev *dev)
{
	int ret;
	int err;
	struct switchtec_diag_port_eye_cmd in = {
		.sub_cmd = MRPC_EYE_OBSERVE_CANCEL,
	};

	ret = switchtec_diag_eye_cmd(dev, &in, sizeof(in));

	/* Add delay so hardware can stop completely */
	err = errno;
	usleep(200000);
	errno = err;

	return ret;
}

/**
 * @brief Start a new Eye Capture (Harpoon)
 * @param[in]  dev	       Switchtec device handle
 * @param[in]  lane_mask       Bitmap of the lanes to capture
 * @param[in]  capture_depth   A maximum of 2^(capture_depth)-1 bits
 * 					will be analyzed.
 * 
 * @return 0 on success, error code on failure
 */
int switchtec_gen5_diag_eye_run(struct switchtec_dev *dev,
				int lane_mask[4], int capture_depth)
{
	/* Need to pass an output buffer to switchtec_cmd() */
	int ret, err, out;
	struct switchtec_gen5_diag_eye_run_in in = {
		.sub_cmd = MRPC_EYE_CAP_RUN_GEN5,
		.capture_depth = capture_depth,
		.timeout_disable = 1,
		.lane_mask[0] = lane_mask[0],
		.lane_mask[1] = lane_mask[1],
		.lane_mask[2] = lane_mask[2],
		.lane_mask[3] = lane_mask[3],
	};

	ret = switchtec_cmd(dev, MRPC_GEN5_EYE_CAPTURE, &in, sizeof(in),
				(void*)&out, sizeof(out));
	
	/* Add delay so hardware has enough time to start */
	err = errno;
	usleep(200000);
	errno = err;

	return ret;
}

/**
 * @brief Get Eye Capture status (Harpoon)
 * @param[in]  dev	       Switchtec device handle
 * @param[out] status	   Eye Capture status
 * 
 * @return 0 on success, error code on failure
 */
int switchtec_gen5_diag_eye_status(struct switchtec_dev *dev,
				int* status)
{
	struct switchtec_gen5_diag_eye_status_in in = {
		.sub_cmd = MRPC_EYE_CAP_STATUS_GEN5,
	};
	struct switchtec_gen5_diag_eye_status_out out;
	int ret;

	ret = switchtec_cmd(dev, MRPC_GEN5_EYE_CAPTURE, &in, sizeof(in),
				&out, sizeof(out));

	*status = out.status;

	return ret;
}

/**
 * @brief Read data from an Eye Capture (Harpoon)
 * @param[in]  dev	       Switchtec device handle
 * @param[in]  lane_id	   Lane ID for the capture data
 * @param[in]  bin		   Bin number [0-63]
 * @param[out] num_phases  Total number of phases (30 or 60)
 * @param[out] ber_data	   BER for each phase for this bin
 * 
 * @return 0 on success, error code on failure
 */
int switchtec_gen5_diag_eye_read(struct switchtec_dev *dev, int lane_id,
				int bin, int* num_phases, double* ber_data)
{
	struct switchtec_gen5_diag_eye_read_in in = {
		.sub_cmd = MRPC_EYE_CAP_READ_GEN5,
		.lane_id = lane_id,
		.bin = bin,
	};
	struct switchtec_gen5_diag_eye_read_out out;
	int i, ret;

	ret = switchtec_cmd(dev, MRPC_GEN5_EYE_CAPTURE, &in, sizeof(in),
				&out, sizeof(out));
	if (ret)
		return ret;

	*num_phases = out.num_phases;

	for(i = 0; i < out.num_phases; i++) {
		ber_data[i] = le64toh(out.ber_data[i]) / 281474976710656.;
	}

	return ret;
}

/**
 * @brief Setup Loopback Mode
 * @param[in]  dev	    Switchtec device handle
 * @param[in]  port_id	    Physical port ID
 * @param[in]  enable       Any enum switchtec_diag_loopback_enable flags
 *			    or'd together to enable specific loopback modes
 * @param[in]  ltssm_speed  LTSSM loopback max speed
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_loopback_set(struct switchtec_dev *dev, int port_id,
		int enable, enum switchtec_diag_ltssm_speed ltssm_speed)
{
	struct switchtec_diag_loopback_in int_in = {
		.sub_cmd = MRPC_LOOPBACK_SET_INT_LOOPBACK,
		.port_id = port_id,
		.enable = enable,
	};
	struct switchtec_diag_loopback_ltssm_in ltssm_in = {
		.sub_cmd = MRPC_LOOPBACK_SET_LTSSM_LOOPBACK,
		.port_id = port_id,
		.enable = !!(enable & SWITCHTEC_DIAG_LOOPBACK_LTSSM),
		.speed = ltssm_speed,
	};
	int ret;

	int_in.type = DIAG_LOOPBACK_RX_TO_TX;
	int_in.enable = !!(enable & SWITCHTEC_DIAG_LOOPBACK_RX_TO_TX);

	ret = switchtec_cmd(dev, MRPC_INT_LOOPBACK, &int_in,
			    sizeof(int_in), NULL, 0);
	if (ret)
		return ret;

	int_in.type = DIAG_LOOPBACK_TX_TO_RX;
	int_in.enable = !!(enable & SWITCHTEC_DIAG_LOOPBACK_TX_TO_RX);

	ret = switchtec_cmd(dev, MRPC_INT_LOOPBACK, &int_in,
			    sizeof(int_in), NULL, 0);
	if (ret)
		return ret;

	ret = switchtec_cmd(dev, MRPC_INT_LOOPBACK, &ltssm_in,
			    sizeof(ltssm_in), NULL, 0);
	if (ret)
		return ret;

	return 0;
}

/**
 * @brief Setup Loopback Mode
 * @param[in]  dev	     Switchtec device handle
 * @param[in]  port_id	     Physical port ID
 * @param[out] enabled       Set of enum switchtec_diag_loopback_enable
 *			     indicating which loopback modes are enabled
 * @param[out] ltssm_speed   LTSSM loopback max speed
 *
 * @return 0 on succes, error code on failure
 */
int switchtec_diag_loopback_get(struct switchtec_dev *dev, int port_id,
		int *enabled, enum switchtec_diag_ltssm_speed *ltssm_speed)
{
	struct switchtec_diag_loopback_in int_in = {
		.sub_cmd = MRPC_LOOPBACK_GET_INT_LOOPBACK,
		.port_id = port_id,
		.type = DIAG_LOOPBACK_RX_TO_TX,
	};
	struct switchtec_diag_loopback_ltssm_in lt_in = {
		.sub_cmd = MRPC_LOOPBACK_GET_LTSSM_LOOPBACK,
		.port_id = port_id,
	};
	struct switchtec_diag_loopback_out int_out;
	struct switchtec_diag_loopback_ltssm_out lt_out;
	int ret, en = 0;

	ret = switchtec_cmd(dev, MRPC_INT_LOOPBACK, &int_in, sizeof(int_in),
			    &int_out, sizeof(int_out));
	if (ret)
		return ret;

	if (int_out.enabled)
		en |= SWITCHTEC_DIAG_LOOPBACK_RX_TO_TX;

	int_in.type = DIAG_LOOPBACK_TX_TO_RX;
	ret = switchtec_cmd(dev, MRPC_INT_LOOPBACK, &int_in, sizeof(int_in),
			    &int_out, sizeof(int_out));
	if (ret)
		return ret;

	if (int_out.enabled)
		en |= SWITCHTEC_DIAG_LOOPBACK_TX_TO_RX;

	ret = switchtec_cmd(dev, MRPC_INT_LOOPBACK, &lt_in, sizeof(lt_in),
			    &lt_out, sizeof(lt_out));
	if (ret)
		return ret;

	if (lt_out.enabled)
		en |= SWITCHTEC_DIAG_LOOPBACK_LTSSM;

	if (enabled)
		*enabled = en;

	if (ltssm_speed)
		*ltssm_speed = lt_out.speed;

	return 0;
}

/**
 * @brief Setup Pattern Generator
 * @param[in]  dev	 Switchtec device handle
 * @param[in]  port_id	 Physical port ID
 * @param[in]  type      Pattern type to enable
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_pattern_gen_set(struct switchtec_dev *dev, int port_id,
		enum switchtec_diag_pattern type)
{
	struct switchtec_diag_pat_gen_in in = {
		.sub_cmd = MRPC_PAT_GEN_SET_GEN,
		.port_id = port_id,
		.pattern_type = type,
	};

	return switchtec_cmd(dev, MRPC_PAT_GEN, &in, sizeof(in), NULL, 0);
}

/**
 * @brief Get Pattern Generator set on port
 * @param[in]  dev	 Switchtec device handle
 * @param[in]  port_id	 Physical port ID
 * @param[out] type      Pattern type to enable
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_pattern_gen_get(struct switchtec_dev *dev, int port_id,
		enum switchtec_diag_pattern *type)
{
	struct switchtec_diag_pat_gen_in in = {
		.sub_cmd = MRPC_PAT_GEN_GET_GEN,
		.port_id = port_id,
	};
	struct switchtec_diag_pat_gen_out out;
	int ret;

	ret = switchtec_cmd(dev, MRPC_PAT_GEN, &in, sizeof(in), &out,
			    sizeof(out));
	if (ret)
		return ret;

	if (type)
		*type = out.pattern_type;

	return 0;
}

/**
 * @brief Setup Pattern Monitor
 * @param[in]  dev	 Switchtec device handle
 * @param[in]  port_id	 Physical port ID
 * @param[in]  type      Pattern type to enable
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_pattern_mon_set(struct switchtec_dev *dev, int port_id,
				   enum switchtec_diag_pattern type)
{
	struct switchtec_diag_pat_gen_in in = {
		.sub_cmd = MRPC_PAT_GEN_SET_MON,
		.port_id = port_id,
		.pattern_type = type,
	};

	return switchtec_cmd(dev, MRPC_PAT_GEN, &in, sizeof(in), NULL, 0);
}

/**
 * @brief Get Pattern Monitor
 * @param[in]  dev	 Switchtec device handle
 * @param[in]  port_id	 Physical port ID
 * @param[out] type      Pattern type to enable
 * @param[out] err_cnt   Number of errors seen
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_pattern_mon_get(struct switchtec_dev *dev, int port_id,
		int lane_id, enum switchtec_diag_pattern *type,
		unsigned long long *err_cnt)
{
	struct switchtec_diag_pat_gen_in in = {
		.sub_cmd = MRPC_PAT_GEN_GET_MON,
		.port_id = port_id,
		.lane_id = lane_id,
	};
	struct switchtec_diag_pat_gen_out out;
	int ret;

	ret = switchtec_cmd(dev, MRPC_PAT_GEN, &in, sizeof(in), &out,
			    sizeof(out));
	if (ret)
		return ret;

	if (type)
		*type = out.pattern_type;

	if (err_cnt)
		*err_cnt = (htole32(out.err_cnt_lo) |
			    ((uint64_t)htole32(out.err_cnt_hi) << 32));

	return 0;
}

/**
 * @brief Inject error into pattern generator
 * @param[in]  dev	 Switchtec device handle
 * @param[in]  port_id	 Physical port ID
 * @param[in] err_cnt   Number of errors seen
 *
 * Injects up to err_cnt errors into each lane of the TX port. It's
 * recommended that the err_cnt be less than 1000, otherwise the
 * firmware runs the risk of consuming too many resources and crashing.
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_pattern_inject(struct switchtec_dev *dev, int port_id,
				  unsigned int err_cnt)
{
	struct switchtec_diag_pat_gen_inject in = {
		.sub_cmd = MRPC_PAT_GEN_INJ_ERR,
		.port_id = port_id,
		.err_cnt = err_cnt,
	};
	int ret;

	ret = switchtec_cmd(dev, MRPC_PAT_GEN, &in, sizeof(in), NULL, 0);
	if (ret)
		return ret;

	return 0;
}

/**
 * @brief Get the receiver object
 * @param[in]  dev	Switchtec device handle
 * @param[in]  port_id	Physical port ID
 * @param[in]  lane_id  Lane ID
 * @param[in]  link     Current or previous link-up
 * @param[out] res      Resulting receiver object
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_rcvr_obj(struct switchtec_dev *dev, int port_id,
		int lane_id, enum switchtec_diag_link link,
		struct switchtec_rcvr_obj *res)
{
	struct switchtec_diag_rcvr_obj_dump_out out = {};
	struct switchtec_diag_rcvr_obj_dump_in in = {
		.port_id = port_id,
		.lane_id = lane_id,
	};
	struct switchtec_diag_ext_recv_obj_dump_in ext_in = {
		.sub_cmd = MRPC_EXT_RCVR_OBJ_DUMP_PREV,
		.port_id = port_id,
		.lane_id = lane_id,
	};
	int i, ret;

	if (!res) {
		errno = -EINVAL;
		return -1;
	}

	if (link == SWITCHTEC_DIAG_LINK_CURRENT) {
		ret = switchtec_cmd(dev, MRPC_RCVR_OBJ_DUMP, &in, sizeof(in),
				    &out, sizeof(out));
	} else if (link == SWITCHTEC_DIAG_LINK_PREVIOUS) {
		ret = switchtec_cmd(dev, MRPC_EXT_RCVR_OBJ_DUMP, &ext_in,
				    sizeof(ext_in), &out, sizeof(out));
	} else {
		errno = -EINVAL;
		return -1;
	}

	if (ret)
		return -1;

	res->port_id = out.port_id;
	res->lane_id = out.lane_id;
	res->ctle = out.ctle;
	res->target_amplitude = out.target_amplitude;
	res->speculative_dfe = out.speculative_dfe;
	for (i = 0; i < ARRAY_SIZE(res->dynamic_dfe); i++)
		res->dynamic_dfe[i] = out.dynamic_dfe[i];

	return 0;
}

/**
 * @brief Get the Gen5 port equalization TX coefficients
 * @param[in]  dev	Switchtec device handle
 * @param[in]  port_id	Physical port ID
 * @param[in]  end      Get coefficents for the Local or the Far End
 * @param[out] res      Resulting port equalization coefficients
 *
 * @return 0 on success, error code on failure
 */
int switchtec_gen5_diag_port_eq_tx_coeff(struct switchtec_dev *dev, int port_id,
		enum switchtec_diag_end end, enum switchtec_diag_link link,
		struct switchtec_port_eq_coeff *res)
{
	struct switchtec_port_eq_coeff *loc_out;
	struct switchtec_rem_port_eq_coeff *rem_out;
	struct switchtec_port_eq_coeff_in *in;
	uint8_t *buf;
	uint32_t buf_size;
	uint32_t in_size = sizeof(struct switchtec_port_eq_coeff_in);
	uint32_t out_size = 0;
	int ret = 0;
	int i;

	if (!res) {
		fprintf(stderr, "Error inval output buffer\n");
		errno = -EINVAL;
		return -1;
	}

	buf_size = in_size;
	if (end == SWITCHTEC_DIAG_LOCAL) {
		buf_size += sizeof(struct switchtec_port_eq_coeff);
		out_size = sizeof(struct switchtec_port_eq_coeff);
	} else if (end == SWITCHTEC_DIAG_FAR_END) {
		buf_size += sizeof(struct switchtec_rem_port_eq_coeff);
		out_size = sizeof(struct switchtec_rem_port_eq_coeff);
	} else {
		fprintf(stderr, "Error inval end option\n");
		errno = -EINVAL;
	}
	buf = (uint8_t *)malloc(buf_size);
	if (!buf) {
		fprintf(stderr, "Error in buffer alloc\n");
		errno = -ENOMEM;
		return -1;
	}

	in = (struct switchtec_port_eq_coeff_in *)buf;
	in->op_type = DIAG_PORT_EQ_STATUS_OP_PER_PORT;
	in->phys_port_id = port_id;
	in->lane_id = 0;
	in->dump_type = LANE_EQ_DUMP_TYPE_CURR;

	if (link == SWITCHTEC_DIAG_LINK_PREVIOUS) {
		in->dump_type = LANE_EQ_DUMP_TYPE_PREV;
		in->prev_rate = PCIE_LINK_RATE_GEN5;
	}

	if (end == SWITCHTEC_DIAG_LOCAL) {
		in->cmd = MRPC_GEN5_PORT_EQ_LOCAL_TX_COEFF_DUMP;
		loc_out = (struct switchtec_port_eq_coeff *)&buf[in_size];
		ret = switchtec_cmd(dev, MRPC_PORT_EQ_STATUS, in, in_size,
							loc_out, out_size);
		if (ret) {
			fprintf(stderr, "Error in switchtec cmd:%d\n", ret);
			goto end;
		}
	} else if (end == SWITCHTEC_DIAG_FAR_END) {
		in->cmd = MRPC_GEN5_PORT_EQ_FAR_END_TX_COEFF_DUMP;
		rem_out = (struct switchtec_rem_port_eq_coeff *)&buf[in_size];
		ret = switchtec_cmd(dev, MRPC_PORT_EQ_STATUS, in, in_size,
							rem_out, out_size);
		if (ret) {
			fprintf(stderr, "Error in switchtec cmd:%d\n", ret);
			goto end;
		}
	} else {
		fprintf(stderr, "Error inval end request\n");
		errno = -EINVAL;
		goto end;
	}

	if (end == SWITCHTEC_DIAG_LOCAL) {
		res->lane_cnt = loc_out->lane_cnt + 1;
		for (i = 0; i < res->lane_cnt; i++) {
			res->cursors[i].pre = loc_out->cursors[i].pre;
			res->cursors[i].post = loc_out->cursors[i].post;
		}
	} else {
		res->lane_cnt = rem_out->lane_cnt + 1;
		for (i = 0; i < res->lane_cnt; i++) {
			res->cursors[i].pre = rem_out->cursors[i].pre;
			res->cursors[i].post = rem_out->cursors[i].post;
		}
	}

end:
	if (buf)
		free(buf);

	return ret;
}

/**
 * @brief Get the Gen4 port equalization TX coefficients
 * @param[in]  dev	Switchtec device handle
 * @param[in]  port_id	Physical port ID
 * @param[in]  end      Get coefficents for the Local or the Far End
 * @param[out] res      Resulting port equalization coefficients
 *
 * @return 0 on success, error code on failure
 */
int switchtec_gen4_diag_port_eq_tx_coeff(struct switchtec_dev *dev, int port_id,
		enum switchtec_diag_end end, enum switchtec_diag_link link,
		struct switchtec_port_eq_coeff *res)
{
	struct switchtec_diag_port_eq_status_out out = {};
	struct switchtec_diag_port_eq_status_in in = {
		.op_type = DIAG_PORT_EQ_STATUS_OP_PER_PORT,
		.port_id = port_id,
	};
	struct switchtec_diag_ext_dump_coeff_prev_in in_prev = {
		.op_type = DIAG_PORT_EQ_STATUS_OP_PER_PORT,
		.port_id = port_id,
	};
	int ret, i;

	if (!res) {
		errno = -EINVAL;
		return -1;
	}

	if (end == SWITCHTEC_DIAG_LOCAL) {
		in.sub_cmd = MRPC_PORT_EQ_LOCAL_TX_COEFF_DUMP;
		in_prev.sub_cmd = MRPC_EXT_RCVR_OBJ_DUMP_LOCAL_TX_COEFF_PREV;
	} else if (end == SWITCHTEC_DIAG_FAR_END) {
		in.sub_cmd = MRPC_PORT_EQ_FAR_END_TX_COEFF_DUMP;
		in_prev.sub_cmd = MRPC_EXT_RCVR_OBJ_DUMP_FAR_END_TX_COEFF_PREV;
	} else {
		errno = -EINVAL;
		return -1;
	}

	if (link == SWITCHTEC_DIAG_LINK_CURRENT) {
		ret = switchtec_cmd(dev, MRPC_PORT_EQ_STATUS, &in, sizeof(in),
				    &out, sizeof(out));
	} else if (link == SWITCHTEC_DIAG_LINK_PREVIOUS) {
		ret = switchtec_cmd(dev, MRPC_EXT_RCVR_OBJ_DUMP, &in_prev,
				    sizeof(in_prev), &out, sizeof(out));
	} else {
		errno = -EINVAL;
		return -1;
	}

	if (ret)
		return -1;

	res->lane_cnt = out.lane_id + 1;
	for (i = 0; i < res->lane_cnt; i++) {
		res->cursors[i].pre = out.cursors[i].pre;
		res->cursors[i].post = out.cursors[i].post;
	}

	return 0;
}

/**
 * @brief Get the port equalization TX coefficients
 * @param[in]  dev	Switchtec device handle
 * @param[in]  port_id	Physical port ID
 * @param[in]  end      Get coefficents for the Local or the Far End
 * @param[out] res      Resulting port equalization coefficients
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_port_eq_tx_coeff(struct switchtec_dev *dev, int port_id,
		enum switchtec_diag_end end, enum switchtec_diag_link link,
		struct switchtec_port_eq_coeff *res)
{
	int ret = -1;

	if (switchtec_is_gen5(dev)) {
		ret = switchtec_gen5_diag_port_eq_tx_coeff(dev, port_id, end,
				link, res);
	} else if (switchtec_is_gen4(dev)){
		ret = switchtec_gen4_diag_port_eq_tx_coeff(dev, port_id, end,
				link, res);
	}

	return ret;
}

/**
 * @brief Get the Gen5 far end TX equalization table
 * @param[in]  dev	Switchtec device handle
 * @param[in]  port_id	Physical port ID
 * @param[out] res      Resulting port equalization table
 *
 * @return 0 on success, error code on failure
 */
int switchtec_gen5_diag_port_eq_tx_table(struct switchtec_dev *dev, int port_id,
					enum switchtec_diag_link link,
					struct switchtec_port_eq_table *res)
{
	struct switchtec_port_eq_table_dump eq_tx_table;
	struct switchtec_gen5_port_eq_table *out = &eq_tx_table.out;
	struct switchtec_port_eq_table_in *in = &eq_tx_table.in;
	int ret, i;

	in->sub_cmd = MRPC_GEN5_PORT_EQ_FAR_END_TX_EQ_TABLE_DUMP;
	in->port_id = port_id;


	if (!res) {
		errno = -EINVAL;
		return -1;
	}

	in->dump_type = LANE_EQ_DUMP_TYPE_CURR;
	in->prev_rate = 0;

	if (link == SWITCHTEC_DIAG_LINK_PREVIOUS) {
		in->dump_type = LANE_EQ_DUMP_TYPE_PREV;
		in->prev_rate = PCIE_LINK_RATE_GEN5;
	}

	ret = switchtec_cmd(dev, MRPC_PORT_EQ_STATUS, in,
						sizeof(struct switchtec_port_eq_table_in),
						out,
						sizeof(struct switchtec_gen5_port_eq_table));
	if (ret)
		return -1;

	res->lane_id = out->lane_id;
	res->step_cnt = out->step_cnt;

	for (i = 0; i < res->step_cnt; i++) {
		res->steps[i].pre_cursor		= out->steps[i].pre_cursor;
		res->steps[i].post_cursor		= out->steps[i].post_cursor;
		res->steps[i].fom				= 0;
		res->steps[i].pre_cursor_up		= 0;
		res->steps[i].post_cursor_up	= 0;
		res->steps[i].error_status		= out->steps[i].error_status;
		res->steps[i].active_status		= out->steps[i].active_status;
		res->steps[i].speed				= out->steps[i].speed;
	}

	return 0;
}

/**
 * @brief Get the Gen4 far end TX equalization table
 * @param[in]  dev	Switchtec device handle
 * @param[in]  port_id	Physical port ID
 * @param[out] res      Resulting port equalization table
 *
 * @return 0 on success, error code on failure
 */
int switchtec_gen4_diag_port_eq_tx_table(struct switchtec_dev *dev, int port_id,
				    enum switchtec_diag_link link,
				    struct switchtec_port_eq_table *res)
{
	struct switchtec_diag_port_eq_table_out out = {};
	struct switchtec_diag_port_eq_status_in2 in = {
		.sub_cmd = MRPC_PORT_EQ_FAR_END_TX_EQ_TABLE_DUMP,
		.port_id = port_id,
	};
	struct switchtec_diag_port_eq_status_in2 in_prev = {
		.sub_cmd = MRPC_EXT_RCVR_OBJ_DUMP_EQ_TX_TABLE_PREV,
		.port_id = port_id,
	};
	int ret, i;

	if (!res) {
		errno = -EINVAL;
		return -1;
	}

	if (link == SWITCHTEC_DIAG_LINK_CURRENT) {
		ret = switchtec_cmd(dev, MRPC_PORT_EQ_STATUS, &in, sizeof(in),
				    &out, sizeof(out));
	} else if (link == SWITCHTEC_DIAG_LINK_PREVIOUS) {
		ret = switchtec_cmd(dev, MRPC_EXT_RCVR_OBJ_DUMP, &in_prev,
				    sizeof(in_prev), &out, sizeof(out));
	} else {
		errno = -EINVAL;
		return -1;
	}

	if (ret)
		return -1;

	res->lane_id = out.lane_id;
	res->step_cnt = out.step_cnt;
	for (i = 0; i < res->step_cnt; i++) {
		res->steps[i].pre_cursor     = out.steps[i].pre_cursor;
		res->steps[i].post_cursor    = out.steps[i].post_cursor;
		res->steps[i].fom            = out.steps[i].fom;
		res->steps[i].pre_cursor_up  = out.steps[i].pre_cursor_up;
		res->steps[i].post_cursor_up = out.steps[i].post_cursor_up;
		res->steps[i].error_status   = out.steps[i].error_status;
		res->steps[i].active_status  = out.steps[i].active_status;
		res->steps[i].speed          = out.steps[i].speed;
	}

	return 0;
}

/**
 * @brief Get the far end TX equalization table
 * @param[in]  dev	Switchtec device handle
 * @param[in]  port_id	Physical port ID
 * @param[out] res      Resulting port equalization table
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_port_eq_tx_table(struct switchtec_dev *dev, int port_id,
				    enum switchtec_diag_link link,
				    struct switchtec_port_eq_table *res)
{
	int ret = -1;


	if (switchtec_is_gen5(dev)) {
		ret = switchtec_gen5_diag_port_eq_tx_table(dev, port_id,
				link, res);
	} else if (switchtec_is_gen4(dev)){
		ret = switchtec_gen4_diag_port_eq_tx_table(dev, port_id,
				link, res);
	}

	return ret;
}

/**
 * @brief Get the Gen5 equalization FS/LF
 * @param[in]  dev	Switchtec device handle
 * @param[in]  port_id	Physical port ID
 * @param[in]  lane_id	Physical port ID
 * @param[in]  end      Get coefficents for the Local or the Far End
 * @param[out] res      Resulting FS/LF values
 *
 * @return 0 on success, error code on failure
 */
int switchtec_gen5_diag_port_eq_tx_fslf(struct switchtec_dev *dev, int port_id,
				   int lane_id, enum switchtec_diag_end end,
				   enum switchtec_diag_link link,
				   struct switchtec_port_eq_tx_fslf *res)
{
	struct switchtec_port_eq_tx_fslf_dump fslf;
	struct switchtec_port_eq_tx_fslf_in *in = &fslf.in;
	struct switchtec_port_eq_tx_fslf_out *out = &fslf.out;
	int ret;

	if (!res) {
		errno = -EINVAL;
		return -1;
	}

	in->port_id = port_id;
	in->lane_id = lane_id;


	if (end == SWITCHTEC_DIAG_LOCAL) {
		in->sub_cmd = MRPC_GEN5_PORT_EQ_LOCAL_TX_FSLF_DUMP;
	} else if (end == SWITCHTEC_DIAG_FAR_END) {
		in->sub_cmd = MRPC_GEN5_PORT_EQ_FAR_END_TX_FSLF_DUMP;
	} else {
		errno = -EINVAL;
		return -1;
	}

	if (link == SWITCHTEC_DIAG_LINK_CURRENT) {
		in->dump_type = LANE_EQ_DUMP_TYPE_CURR;
	} else {
		in->dump_type = LANE_EQ_DUMP_TYPE_PREV;
		in->prev_rate = PCIE_LINK_RATE_GEN5;
	}

	ret = switchtec_cmd(dev, MRPC_PORT_EQ_STATUS, in,
				sizeof(struct switchtec_port_eq_tx_fslf_in),
				out,
				sizeof(struct switchtec_port_eq_tx_fslf_out));
	if (ret)
		return -1;

	res->fs = out->fs;
	res->lf = out->lf;

	return 0;
}

/**
 * @brief Get the Gen4 equalization FS/LF
 * @param[in]  dev	Switchtec device handle
 * @param[in]  port_id	Physical port ID
 * @param[in]  lane_id	Physical port ID
 * @param[in]  end      Get coefficents for the Local or the Far End
 * @param[out] res      Resulting FS/LF values
 *
 * @return 0 on success, error code on failure
 */
int switchtec_gen4_diag_port_eq_tx_fslf(struct switchtec_dev *dev, int port_id,
				int lane_id, enum switchtec_diag_end end,
				enum switchtec_diag_link link,
				struct switchtec_port_eq_tx_fslf *res)
{
	struct switchtec_diag_port_eq_tx_fslf_out out = {};
	struct switchtec_diag_port_eq_status_in2 in = {
		.port_id = port_id,
		.lane_id = lane_id,
	};
	struct switchtec_diag_ext_recv_obj_dump_in in_prev = {
		.port_id = port_id,
		.lane_id = lane_id,
	};
	int ret;

	if (!res) {
		errno = -EINVAL;
		return -1;
	}

	if (end == SWITCHTEC_DIAG_LOCAL) {
		in.sub_cmd = MRPC_PORT_EQ_LOCAL_TX_FSLF_DUMP;
		in_prev.sub_cmd = MRPC_EXT_RCVR_OBJ_DUMP_LOCAL_TX_FSLF_PREV;
	} else if (end == SWITCHTEC_DIAG_FAR_END) {
		in.sub_cmd = MRPC_PORT_EQ_FAR_END_TX_FSLF_DUMP;
		in_prev.sub_cmd = MRPC_EXT_RCVR_OBJ_DUMP_FAR_END_TX_FSLF_PREV;
	} else {
		errno = -EINVAL;
		return -1;
	}

	if (link == SWITCHTEC_DIAG_LINK_CURRENT) {
		ret = switchtec_cmd(dev, MRPC_PORT_EQ_STATUS, &in, sizeof(in),
				    &out, sizeof(out));
	} else if (link == SWITCHTEC_DIAG_LINK_PREVIOUS) {
		ret = switchtec_cmd(dev, MRPC_EXT_RCVR_OBJ_DUMP, &in_prev,
				    sizeof(in_prev), &out, sizeof(out));
	} else {
		errno = -EINVAL;
		return -1;
	}

	if (ret)
		return -1;

	res->fs = out.fs;
	res->lf = out.lf;

	return 0;
}

/**
 * @brief Get the equalization FS/LF
 * @param[in]  dev	Switchtec device handle
 * @param[in]  port_id	Physical port ID
 * @param[in]  lane_id	Physical port ID
 * @param[in]  end      Get coefficents for the Local or the Far End
 * @param[out] res      Resulting FS/LF values
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_port_eq_tx_fslf(struct switchtec_dev *dev, int port_id,
				 int lane_id, enum switchtec_diag_end end,
				 enum switchtec_diag_link link,
				 struct switchtec_port_eq_tx_fslf *res)
{
	int ret = -1;

	if (switchtec_is_gen5(dev)) {
		ret = switchtec_gen5_diag_port_eq_tx_fslf(dev, port_id,
				lane_id, end, link, res);
	} else if (switchtec_is_gen4(dev)){
		ret = switchtec_gen4_diag_port_eq_tx_fslf(dev, port_id,
				lane_id, end, link, res);
	}

	return ret;
}

/**
 * @brief Get the Extended Receiver Object
 * @param[in]  dev 	Switchtec device handle
 * @param[in]  port_id	Physical port ID
 * @param[in]  lane_id  Lane ID
 * @param[in]  link     Current or previous link-up
 * @param[out] res      Resulting receiver object
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_rcvr_ext(struct switchtec_dev *dev, int port_id,
			    int lane_id, enum switchtec_diag_link link,
			    struct switchtec_rcvr_ext *res)
{
	struct switchtec_diag_rcvr_ext_out out = {};
	struct switchtec_diag_ext_recv_obj_dump_in in = {
		.port_id = port_id,
		.lane_id = lane_id,
	};
	int ret;

	if (!res) {
		errno = -EINVAL;
		return -1;
	}

	if (link == SWITCHTEC_DIAG_LINK_CURRENT) {
		in.sub_cmd = MRPC_EXT_RCVR_OBJ_DUMP_RCVR_EXT;
	} else if (link == SWITCHTEC_DIAG_LINK_PREVIOUS) {
		in.sub_cmd = MRPC_EXT_RCVR_OBJ_DUMP_RCVR_EXT_PREV;
	} else {
		errno = -EINVAL;
		return -1;
	}

	ret = switchtec_cmd(dev, MRPC_EXT_RCVR_OBJ_DUMP, &in, sizeof(in),
			    &out, sizeof(out));
	if (ret)
		return -1;

	res->ctle2_rx_mode = out.ctle2_rx_mode;
	res->dtclk_9 = out.dtclk_9;
	res->dtclk_8_6 = out.dtclk_8_6;
	res->dtclk_5 = out.dtclk_5;

	return 0;
}

/**
 * @brief Get the permission table
 * @param[in]  dev	Switchtec device handle
 * @param[out] table    Resulting MRPC permission table
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_perm_table(struct switchtec_dev *dev,
			      struct switchtec_mrpc table[MRPC_MAX_ID])
{
	uint32_t perms[(MRPC_MAX_ID + 31) / 32];
	int i, ret;

	ret = switchtec_cmd(dev, MRPC_MRPC_PERM_TABLE_GET, NULL, 0,
			    perms, sizeof(perms));
	if (ret)
		return -1;

	for (i = 0; i < MRPC_MAX_ID; i++) {
		if (perms[i >> 5] & (1 << (i & 0x1f))) {
			if (switchtec_mrpc_table[i].tag) {
				table[i] = switchtec_mrpc_table[i];
			} else {
				table[i].tag = "UNKNOWN";
				table[i].desc = "Unknown MRPC Command";
				table[i].reserved = true;
			}
		} else {
			table[i].tag = NULL;
			table[i].desc = NULL;
		}
	}

	return 0;
}

/**
 * @brief Control the refclk output for a stack
 * @param[in]  dev	Switchtec device handle
 * @param[in]  stack_id	Stack ID to control the refclk of
 * @param[in]  en	Set to true to enable, false to disable
 *
 * @return 0 on success, error code on failure
 */
int switchtec_diag_refclk_ctl(struct switchtec_dev *dev, int stack_id, bool en)
{
	struct switchtec_diag_refclk_ctl_in cmd = {
		.sub_cmd = en ? MRPC_REFCLK_S_ENABLE : MRPC_REFCLK_S_DISABLE,
		.stack_id = stack_id,
	};

	return switchtec_cmd(dev, MRPC_REFCLK_S, &cmd, sizeof(cmd), NULL, 0);
}

/**
 * @brief Get the LTSSM log of a port on a switchtec device
 * @param[in]	dev    Switchtec device handle
 * @param[in]	port   Switchtec Port
 * @param[inout] log_count number of log entries
 * @param[out] log    A pointer to an array containing the log
 *
 */
int switchtec_diag_ltssm_log_gen4(struct switchtec_dev *dev,
			     int port, int *log_count,
			     struct switchtec_diag_ltssm_log *log_data)
{
	struct {
		uint8_t sub_cmd;
		uint8_t port;
		uint8_t freeze;
		uint8_t unused;
	} ltssm_freeze;

	struct {
		uint8_t sub_cmd;
		uint8_t port;
	} status;
	struct {
		uint32_t w0_trigger_count;
		uint32_t w1_trigger_count;
		uint8_t log_num;
	} status_output;

	struct {
		uint8_t sub_cmd;
		uint8_t port;
		uint8_t log_index;
		uint8_t no_of_logs;
	} log_dump;
	struct {
		uint32_t dw0;
		uint32_t dw1;
	} log_dump_out[256];

	uint32_t dw1;
	uint32_t dw0;
	int major;
	int minor;
	int rate;
	int ret;
	int i;

	/* freeze logs */
	ltssm_freeze.sub_cmd = 14;
	ltssm_freeze.port = port;
	ltssm_freeze.freeze = 1;

	ret = switchtec_cmd(dev, MRPC_DIAG_PORT_LTSSM_LOG, &ltssm_freeze,
			    sizeof(ltssm_freeze), NULL, 0);
	if (ret)
		return ret;

	/* get number of entries */
	status.sub_cmd = 13;
	status.port = port;
	ret = switchtec_cmd(dev, MRPC_DIAG_PORT_LTSSM_LOG, &status,
			    sizeof(status), &status_output,
			    sizeof(status_output));
	if (ret)
		return ret;

	if (status_output.log_num < *log_count)
		*log_count = status_output.log_num;

	/* get log data */
	log_dump.sub_cmd = 15;
	log_dump.port = port;
	log_dump.log_index = 0;
	log_dump.no_of_logs = *log_count;
	if(log_dump.no_of_logs <= 126) {
		ret = switchtec_cmd(dev, MRPC_DIAG_PORT_LTSSM_LOG, &log_dump,
				    sizeof(log_dump), log_dump_out,
				    8 * log_dump.no_of_logs);
		if (ret)
			return ret;
	} else {
		log_dump.no_of_logs = 126;
		ret = switchtec_cmd(dev, MRPC_DIAG_PORT_LTSSM_LOG, &log_dump,
				    sizeof(log_dump), log_dump_out,
				    8 * log_dump.no_of_logs);
		if (ret)
			return ret;

		log_dump.log_index = 126;
		log_dump.no_of_logs = *log_count - 126;

		ret = switchtec_cmd(dev, MRPC_DIAG_PORT_LTSSM_LOG, &log_dump,
				    sizeof(log_dump), log_dump_out + 126,
				    8 * log_dump.no_of_logs);
		if (ret)
			return ret;
	}
	for (i = 0; i < *log_count; i++) {
		dw1 = log_dump_out[i].dw1;
		dw0 = log_dump_out[i].dw0;
		rate = (dw0 >> 13) & 0x3;
		major = (dw0 >> 7) & 0xf;
		minor = (dw0 >> 3) & 0xf;

		log_data[i].timestamp = dw1 & 0x3ffffff;
		log_data[i].timestamp_high = 0;
		log_data[i].link_rate = switchtec_gen_transfers[rate + 1];
		log_data[i].link_state = major | (minor << 8);
	}

	/* unfreeze logs */
	ltssm_freeze.sub_cmd = 14;
	ltssm_freeze.port = port;
	ltssm_freeze.freeze = 0;

	ret = switchtec_cmd(dev, MRPC_DIAG_PORT_LTSSM_LOG, &ltssm_freeze,
			    sizeof(ltssm_freeze), NULL, 0);

	return ret;
}

/* Firmware uses this definitions during log dump */
#define MAX_LOG_READ	    63
#define LOG_DATA_OFFSET 	4

/**
 * @brief Get the LTSSM log of a port on a switchtec device
 * @param[in]	dev    Switchtec device handle
 * @param[in]	port   Switchtec Port
 * @param[inout] log_count number of log entries
 * @param[out] log    A pointer to an array containing the log
 *
 */
int switchtec_diag_ltssm_log_gen5(struct switchtec_dev *dev,
			     int port, int *log_count,
			     struct switchtec_diag_ltssm_log *log_data)
{

	struct {
		uint8_t sub_cmd;
		uint8_t port;
		uint8_t freeze;
		uint8_t unused;
	} ltssm_freeze;

	struct {
		uint8_t sub_cmd;
		uint8_t port;
	} status;

	struct {
		uint16_t trig_cnt;
		uint16_t w0_trigger_count;
		uint16_t w1_trigger_count;
		uint16_t time_stamp;
		uint8_t trig_src_stat;
	} status_output;

	struct {
		uint8_t sub_cmd;
		uint8_t port;
		uint16_t log_index;
		uint16_t no_of_logs;
	} log_dump;

	struct log_buffer{
		/* DWORD 0*/
		uint32_t rx_10s:3;
		uint32_t minor:4;
		uint32_t major:6;
		uint32_t link_rate:3;
		uint32_t rlov:1;
		uint32_t reserved0:1;
		/* DWORD 1 */
		uint32_t time_stamp;
		/* DWORD 2 */
		uint32_t ts_high:5;
		uint32_t reserved1:27;
		/*DWORD 3 */
		uint32_t cond;
	};

	struct {
		uint8_t get_status_sub_cmd;
		uint8_t freeze_restore_sub_cmd;
		uint8_t log_dump_sub_cmd;
	} sub_cmd_list;

	int ret;
	int i;
	int cur_idx;
	int avail_log;
	struct log_buffer *buf;
	uint8_t log_dump_out[1024];

	sub_cmd_list.get_status_sub_cmd = 20;
	sub_cmd_list.freeze_restore_sub_cmd = 14;
	sub_cmd_list.log_dump_sub_cmd = 21;

	/* freeze logs */
	ltssm_freeze.sub_cmd = sub_cmd_list.freeze_restore_sub_cmd;
	ltssm_freeze.port = port;
	ltssm_freeze.freeze = 1;

	ret = switchtec_cmd(dev, MRPC_DIAG_PORT_LTSSM_LOG, &ltssm_freeze,
			    sizeof(ltssm_freeze), NULL, 0);
	if (ret)
		return ret;

	/* get number of entries */
	status.sub_cmd = sub_cmd_list.get_status_sub_cmd;
	status.port = port;
	ret = switchtec_cmd(dev, MRPC_DIAG_PORT_LTSSM_LOG, &status,
			    sizeof(status), &status_output,
			    sizeof(status_output));
	if (ret)
		return ret;

	if (status_output.trig_cnt < *log_count)
		*log_count = status_output.trig_cnt;

	cur_idx = 0;
	avail_log = *log_count;

	while ((avail_log > 0)) {

		int log_dump_len;

		/* get log data */
		log_dump.sub_cmd = sub_cmd_list.log_dump_sub_cmd;
		log_dump.port = port;
		log_dump.log_index = cur_idx;
		log_dump.no_of_logs = ((avail_log > MAX_LOG_READ)? MAX_LOG_READ: avail_log);
		log_dump_len = sizeof(struct log_buffer) * log_dump.no_of_logs + LOG_DATA_OFFSET;

		ret = switchtec_cmd(dev, MRPC_DIAG_PORT_LTSSM_LOG, &log_dump,
					sizeof(log_dump), &log_dump_out[0], log_dump_len);
		if (ret) {
			fprintf(stderr, "Error in ltssm dump API\n");
			break;
		}

		buf = (struct log_buffer *)&(log_dump_out[4]);
		for (i = 0; i < log_dump.no_of_logs; i++) {
			int link_state = buf[i].major | (buf[i].minor << 8);
			float link_rate = switchtec_gen_transfers[
								buf[i].link_rate + 1];

			log_data[cur_idx + i].timestamp = buf[i].time_stamp;
			log_data[cur_idx + i].timestamp_high = buf[i].ts_high;
			log_data[cur_idx + i].link_rate = link_rate;
			log_data[cur_idx + i].link_state = link_state;
		}

		cur_idx += log_dump.no_of_logs;
		if (avail_log > log_dump.no_of_logs)
			avail_log = avail_log - log_dump.no_of_logs;
		else
			break;
	};

	/* unfreeze logs */
	ltssm_freeze.sub_cmd = sub_cmd_list.freeze_restore_sub_cmd;
	ltssm_freeze.port = port;
	ltssm_freeze.freeze = 0;

	ret = switchtec_cmd(dev, MRPC_DIAG_PORT_LTSSM_LOG, &ltssm_freeze,
			    sizeof(ltssm_freeze), NULL, 0);

	return ret;
}

/**
 * @brief Call the LTSSM log function based on Gen4/Gen5 device
 * @param[in]	dev    Switchtec device handle
 * @param[in]	port   Switchtec Port
 * @param[inout] log_count number of log entries
 * @param[out] log    A pointer to an array containing the log
 *
 */
int switchtec_diag_ltssm_log(struct switchtec_dev *dev,
			     int port, int *log_count,
			     struct switchtec_diag_ltssm_log *log_data)
{
	int ret = -EINVAL;

	/* support for ltssm-log is available only for Gen4 and Gen5 */
	if (switchtec_is_gen5(dev)) {
		ret = switchtec_diag_ltssm_log_gen5(dev, port, log_count,
					log_data);
	} else if (switchtec_is_gen4(dev)){
		ret = switchtec_diag_ltssm_log_gen4(dev, port, log_count,
					log_data);
	} else {
		fprintf(stderr, "Invalid Gen mode, exit ltssm-log func \n");
	}

	return ret;
}

/**
 * @brief Call the aer event gen function to generate AER events
 * @param[in]   dev    Switchtec device handle
 * @param[in]   port   Switchtec Port
 * @param[in]   aer_error_id aer error bit
 * @param[out]  trigger_event One of the trigger events
 *
 */
int switchtec_aer_event_gen(struct switchtec_dev *dev, int port_id,
		int aer_error_id, int trigger_event)
{
	uint32_t output;
	int ret_val;

	struct switchtec_aer_event_gen_in sub_cmd_id = {
		.sub_cmd = trigger_event,
		.phys_port_id = port_id,
		.err_mask = (1 << aer_error_id),
		.hdr_log[0] = 0,
		.hdr_log[1] = 0,
		.hdr_log[2] = 0,
		.hdr_log[3] = 0
	};

	ret_val = switchtec_cmd(dev, MRPC_AER_GEN, &sub_cmd_id,
					sizeof(sub_cmd_id), &output, sizeof(output));
	return ret_val;
}

/**@}*/
