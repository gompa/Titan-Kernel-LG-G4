/* Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/qpnp/clkdiv.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/switch.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/jack.h>
#include <sound/q6afe-v2.h>
#include <sound/q6core.h>
#include <sound/pcm_params.h>
#include <soc/qcom/liquid_dock.h>
#include "device_event.h"
#include "qdsp6v2/msm-pcm-routing-v2.h"
#include "../codecs/wcd9xxx-common.h"
#include "../codecs/wcd9330.h"

#if defined(CONFIG_SND_SOC_TAS2552)
#include <soc/qcom/lge/board_lge.h>
#endif

#define DRV_NAME "msm8994-asoc-snd"

#define SAMPLING_RATE_8KHZ      8000
#define SAMPLING_RATE_16KHZ     16000
#define SAMPLING_RATE_32KHZ     32000
#define SAMPLING_RATE_48KHZ     48000
#define SAMPLING_RATE_96KHZ     96000
#define SAMPLING_RATE_192KHZ    192000

#define MSM8994_SPK_ON     1
#define MSM8994_SPK_OFF    0

#define MSM_SLIM_0_RX_MAX_CHANNELS    2
#define MSM_SLIM_0_TX_MAX_CHANNELS    4

#define I2S_PCM_SEL_PCM       1
#define I2S_PCM_SEL_I2S       0
#define I2S_PCM_SEL_OFFSET    1

#define WCD9XXX_MBHC_DEF_BUTTONS    8
#define WCD9XXX_MBHC_DEF_RLOADS     5
#define TOMTOM_EXT_CLK_RATE         9600000
#define ADSP_STATE_READY_TIMEOUT_MS    3000

#ifdef CONFIG_SND_USE_TERT_MI2S
#define MAX_PINCTRL_STATE  16

    enum pinctrl_pin_state {
	INTF_NONE = 0x0,     	/* All pins are in sleep state */
	INTF_SEC_AUXPCM =0x1, 	/* SEC_AUX_PCM = active */
	INTF_PRI_MI2S =0x2, 	/* PRI_MI2S = active */
	INTF_QUAT_MI2S =0x4, 	/* QUAT_MI2S = active */
	INTF_TERT_MI2S =0x8, 	/* TERT_MI2S = active */
	INTF_MAX =0x10 			/* */
};
#else
#define MAX_PINCTRL_STATE  8

enum pinctrl_pin_state {
	INTF_NONE = 0x0,     	/* All pins are in sleep state */
	INTF_SEC_AUXPCM =0x1, 	/* SEC_AUX_PCM = active */
	INTF_PRI_MI2S =0x2, 	/* PRI_MI2S = active */
	INTF_QUAT_MI2S =0x4, 	/* QUAT_MI2S = active */
	INTF_MAX =0x8 			/* */
};
#endif /*#ifdef CONFIG_SND_USE_TERT_MI2S*/

struct msm_pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *pctrl[MAX_PINCTRL_STATE];
	u16 pinctrl_state;
};

struct msm8994_asoc_mach_data {
	int mclk_gpio;
	u32 mclk_freq;
	int us_euro_gpio;
	struct msm_pinctrl_info pinctrl_info;
	void __iomem *pri_mux;
	void __iomem *sec_mux;
#ifdef CONFIG_SND_USE_TERT_MI2S
	void __iomem *tert_mux;
#endif /*#ifdef CONFIG_SND_USE_TERT_MI2S*/
#ifdef CONFIG_SND_USE_QUAT_MI2S
    void __iomem *quat_mux;
#endif
};

#ifdef CONFIG_SND_USE_TERT_MI2S

static struct afe_clk_cfg tert_mi2s_clk = {
	AFE_API_VERSION_I2S_CONFIG,
	Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ,
	Q6AFE_LPASS_OSR_CLK_DISABLE,
	Q6AFE_LPASS_CLK_SRC_INTERNAL,
	Q6AFE_LPASS_CLK_ROOT_DEFAULT,
	Q6AFE_LPASS_MODE_CLK1_VALID,
	0,
};

static atomic_t tert_mi2s_rsc_ref;
static int tert_mi2s_sample_rate = SAMPLING_RATE_48KHZ;
static int tert_mi2s_bit_format = SNDRV_PCM_FORMAT_S16_LE;

#ifndef CONFIG_SND_USE_QUAT_MI2S
static int msm_set_pinctrl(struct msm_pinctrl_info *pinctrl_info, u16 state_mask, u16 intf_enable);
#endif /*#ifndef CONFIG_SND_USE_QUAT_MI2S*/

#endif /*#ifdef CONFIG_SND_USE_TERT_MI2S*/

#ifdef CONFIG_SND_USE_QUAT_MI2S
#define MSM_QUAT_MI2S_MASTER
#ifndef CONFIG_SND_SOC_TAS2552
#define MSM_QUAT_MI2S_MCLK
#endif  /*CONFIG_SND_SOC_TAS2552*/

static struct afe_clk_cfg quat_mi2s_clk = {
	AFE_API_VERSION_I2S_CONFIG,
	Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ,
	Q6AFE_LPASS_OSR_CLK_DISABLE,
	Q6AFE_LPASS_CLK_SRC_INTERNAL,
	Q6AFE_LPASS_CLK_ROOT_DEFAULT,
	Q6AFE_LPASS_MODE_CLK1_VALID,
	0,
};


static atomic_t quat_mi2s_rsc_ref;
static int quat_mi2s_sample_rate = SAMPLING_RATE_48KHZ;
static int quat_mi2s_bit_format = SNDRV_PCM_FORMAT_S16_LE;


struct snd_soc_card *g_card;

static int msm_set_pinctrl(struct msm_pinctrl_info *pinctrl_info, u16 state_mask, u16 intf_enable);

#endif /*#ifdef CONFIG_SND_USE_QUAT_MI2S*/


static int slim0_rx_sample_rate = SAMPLING_RATE_48KHZ;
static int slim0_tx_sample_rate = SAMPLING_RATE_48KHZ;
static int slim0_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int slim0_tx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int hdmi_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int msm8994_auxpcm_rate = 8000;
static int slim5_rx_sample_rate = SAMPLING_RATE_48KHZ;
static int slim5_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;

static struct platform_device *spdev;
static int ext_us_amp_gpio = -1;
static int msm8994_spk_control = 1;
static int msm_slim_0_rx_ch = 1;
static int msm_slim_0_tx_ch = 1;
static int msm_vi_feed_tx_ch = 2;
static int msm_slim_5_rx_ch = 1;

static int msm_btsco_rate = SAMPLING_RATE_8KHZ;
static int msm_hdmi_rx_ch = 2;
static int msm_proxy_rx_ch = 2;
static int hdmi_rx_sample_rate = SAMPLING_RATE_48KHZ;
static int msm_pri_mi2s_tx_ch = 2;

static struct mutex cdc_mclk_mutex;
static struct clk *codec_clk;
static int clk_users;
static atomic_t prim_auxpcm_rsc_ref;
static atomic_t sec_auxpcm_rsc_ref;

struct audio_plug_dev {
	int plug_gpio;
	int plug_irq;
	int plug_det;
	struct work_struct irq_work;
	struct switch_dev audio_sdev;
};

static struct audio_plug_dev *msm8994_liquid_dock_dev;

/* Rear panel audio jack which connected to LO1&2 */
static struct audio_plug_dev *apq8094_db_ext_bp_out_dev;
/* Front panel audio jack which connected to LO3&4 */
static struct audio_plug_dev *apq8094_db_ext_fp_in_dev;
/* Front panel audio jack which connected to Line in (ADC6) */
static struct audio_plug_dev *apq8094_db_ext_fp_out_dev;

#ifdef CONFIG_SND_USE_TERT_MI2S
static const char *const pin_states[] = {"pin_state_0",
	"pin_state_1",
	"pin_state_2",
	"pin_state_3",
	"pin_state_4",
	"pin_state_5",
	"pin_state_6",
	"pin_state_7",
	"pin_state_8",
	"pin_state_9",
	"pin_state_10",
	"pin_state_11",
	"pin_state_12",
	"pin_state_13",
	"pin_state_14",
	"pin_state_15",
};
#else
static const char *const pin_states[] = {"pin_state_0",
	"pin_state_1",
	"pin_state_2",
	"pin_state_3",
	"pin_state_4",
	"pin_state_5",
	"pin_state_6",
	"pin_state_7",
};
#endif /* CONFIG_SND_USE_TERT_MI2S */

static const char *const spk_function[] = {"Off", "On"};
static const char *const slim0_rx_ch_text[] = {"One", "Two"};
static const char *const slim5_rx_ch_text[] = {"One", "Two"};
static const char *const vi_feed_ch_text[] = {"One", "Two"};
static const char *const slim0_tx_ch_text[] = {"One", "Two", "Three", "Four",
						"Five", "Six", "Seven",
						"Eight"};
static char const *hdmi_rx_ch_text[] = {"Two", "Three", "Four", "Five",
					"Six", "Seven", "Eight"};
static char const *rx_bit_format_text[] = {"S16_LE", "S24_LE", "S24_3LE"};
static char const *slim5_rx_bit_format_text[] = {"S16_LE", "S24_LE", "S24_3LE"};
static char const *slim0_rx_sample_rate_text[] = {"KHZ_48", "KHZ_96",
					"KHZ_192"};
static char const *slim5_rx_sample_rate_text[] = {"KHZ_48", "KHZ_96",
						  "KHZ_192"};
static const char *const proxy_rx_ch_text[] = {"One", "Two", "Three", "Four",
	"Five",	"Six", "Seven", "Eight"};

static char const *hdmi_rx_sample_rate_text[] = {"KHZ_48", "KHZ_96",
					"KHZ_192"};
static const char *const btsco_rate_text[] = {"8000", "16000"};
static const struct soc_enum msm_btsco_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, btsco_rate_text),
};

static const char *const auxpcm_rate_text[] = {"8000", "16000"};
static const struct soc_enum msm8994_auxpcm_enum[] = {
		SOC_ENUM_SINGLE_EXT(2, auxpcm_rate_text),
};


static void *adsp_state_notifier;
static void *def_codec_mbhc_cal(void);

static int msm_snd_enable_codec_ext_clk(struct snd_soc_codec *codec,
					int enable, bool dapm);

static struct wcd9xxx_mbhc_config mbhc_cfg = {
	.read_fw_bin = false,
	.calibration = NULL,
	.micbias = MBHC_MICBIAS2,
	.anc_micbias = MBHC_MICBIAS2,
	.mclk_cb_fn = msm_snd_enable_codec_ext_clk,
	.mclk_rate = TOMTOM_EXT_CLK_RATE,
#ifdef CONFIG_MACH_LGE
	.gpio_level_insert = 0,
#if defined(CONFIG_SND_LGE_HIDDEN_AUX)
	.detect_extn_cable = true,
#else
	.detect_extn_cable = false,
#endif
#else
	.gpio_level_insert = 1,
	.detect_extn_cable = true,
#endif
#ifdef CONFIG_MACH_LGE
	.micbias_enable_flags = 1 << MBHC_MICBIAS_ENABLE_THRESHOLD_HEADSET |
				1 << MBHC_MICBIAS_ENABLE_REGULAR_HEADSET,
#else
	.micbias_enable_flags = 1 << MBHC_MICBIAS_ENABLE_THRESHOLD_HEADSET,
#endif
	.insert_detect = true,
	.swap_gnd_mic = NULL,
	.cs_enable_flags = (1 << MBHC_CS_ENABLE_POLLING |
			    1 << MBHC_CS_ENABLE_INSERTION |
			    1 << MBHC_CS_ENABLE_REMOVAL |
			    1 << MBHC_CS_ENABLE_DET_ANC),
	.do_recalibration = true,
	.use_vddio_meas = true,
	.enable_anc_mic_detect = false,
	.hw_jack_type = SIX_POLE_JACK,
	.key_code[0] = KEY_MEDIA,
	.key_code[1] = KEY_VOICECOMMAND,
	.key_code[2] = KEY_VOLUMEUP,
	.key_code[3] = KEY_VOLUMEDOWN,
	.key_code[4] = 0,
	.key_code[5] = 0,
	.key_code[6] = 0,
	.key_code[7] = 0,
#ifdef CONFIG_SND_EAR_MIC_SWITCH
	.hph_aud_sw = -1,
#endif
#ifdef CONFIG_SND_SOC_HPH_WA
	.mbhc_det_gpio=0,
#endif

};

static struct afe_clk_cfg mi2s_tx_clk = {
	AFE_API_VERSION_I2S_CONFIG,
	Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ,
	Q6AFE_LPASS_OSR_CLK_DISABLE,
	Q6AFE_LPASS_CLK_SRC_INTERNAL,
	Q6AFE_LPASS_CLK_ROOT_DEFAULT,
	Q6AFE_LPASS_MODE_CLK1_VALID,
	0,
};

static inline int param_is_mask(int p)
{
	return ((p >= SNDRV_PCM_HW_PARAM_FIRST_MASK) &&
			(p <= SNDRV_PCM_HW_PARAM_LAST_MASK));
}

static inline struct snd_mask *param_to_mask(struct snd_pcm_hw_params *p, int n)
{
	return &(p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK]);
}

static void param_set_mask(struct snd_pcm_hw_params *p, int n, unsigned bit)
{
	if (bit >= SNDRV_MASK_MAX)
		return;
	if (param_is_mask(n)) {
		struct snd_mask *m = param_to_mask(p, n);
		m->bits[0] = 0;
		m->bits[1] = 0;
		m->bits[bit >> 5] |= (1 << (bit & 31));
	}
}

static irqreturn_t msm8994_audio_plug_device_irq_handler(int irq, void *dev)
{
	struct audio_plug_dev *dock_dev = dev;

	/* switch speakers should not run in interrupt context */
	schedule_work(&dock_dev->irq_work);
	return IRQ_HANDLED;
}

static int msm8994_audio_plug_device_init
				(struct audio_plug_dev *audio_plug_dev,
				 int plug_gpio,
				 char *node_name,
				 char *switch_dev_name,
				 void (*irq_work)(struct work_struct *work))
{
	int ret = 0;

	/* awaike by plug in device OR unplug one of them */
	u32 plug_irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;

	audio_plug_dev =
		kzalloc(sizeof(*audio_plug_dev), GFP_KERNEL);
	if (!audio_plug_dev) {
		pr_err("audio_plug_device alloc fail. dev_name= %s\n",
					node_name);
		ret = -ENOMEM;
		goto exit;
	}

	audio_plug_dev->plug_gpio = plug_gpio;

	ret = gpio_request(audio_plug_dev->plug_gpio,
					   node_name);
	if (ret) {
		pr_err("%s:failed request audio_plug_device device_name = %s err= 0x%x\n",
			__func__, node_name, ret);
		ret = -EINVAL;
		goto release_audio_plug_device;
	}

	ret = gpio_direction_input(audio_plug_dev->plug_gpio);

	audio_plug_dev->plug_det =
			gpio_get_value(audio_plug_dev->plug_gpio);
	audio_plug_dev->plug_irq =
		gpio_to_irq(audio_plug_dev->plug_gpio);

	ret = request_irq(audio_plug_dev->plug_irq,
				msm8994_audio_plug_device_irq_handler,
				plug_irq_flags,
				node_name,
				audio_plug_dev);
	if (ret < 0) {
		pr_err("%s: Request Irq Failed err = %d\n",
				__func__, ret);
		goto fail_gpio;
	}

	audio_plug_dev->audio_sdev.name = switch_dev_name;
	if (switch_dev_register(
		&audio_plug_dev->audio_sdev) < 0) {
		pr_err("%s: audio plug device register in switch diretory failed\n",
		__func__);
		goto fail_switch_dev;
	}

	switch_set_state(&audio_plug_dev->audio_sdev,
					!(audio_plug_dev->plug_det));

	/*notify to audio deamon*/
	sysfs_notify(&audio_plug_dev->audio_sdev.dev->kobj,
				NULL, "state");

	INIT_WORK(&audio_plug_dev->irq_work,
			irq_work);

	return 0;

fail_switch_dev:
	free_irq(audio_plug_dev->plug_irq,
				audio_plug_dev);
fail_gpio:
	gpio_free(audio_plug_dev->plug_gpio);
release_audio_plug_device:
	kfree(audio_plug_dev);
exit:
	audio_plug_dev = NULL;
	return ret;

}

static void msm8994_audio_plug_device_remove
				(struct audio_plug_dev *audio_plug_dev)
{
	if (audio_plug_dev != NULL) {
		switch_dev_unregister(&audio_plug_dev->audio_sdev);

		if (audio_plug_dev->plug_irq)
			free_irq(audio_plug_dev->plug_irq,
				 audio_plug_dev);

		if (audio_plug_dev->plug_gpio)
			gpio_free(audio_plug_dev->plug_gpio);

		kfree(audio_plug_dev);
		audio_plug_dev = NULL;
	}
}

static void msm8994_liquid_docking_irq_work(struct work_struct *work)
{
	struct audio_plug_dev *dock_dev =
		container_of(work, struct audio_plug_dev, irq_work);

	dock_dev->plug_det =
		gpio_get_value(dock_dev->plug_gpio);

	switch_set_state(&dock_dev->audio_sdev, dock_dev->plug_det);
	/*notify to audio deamon*/
	sysfs_notify(&dock_dev->audio_sdev.dev->kobj, NULL, "state");
}

static int msm8994_liquid_dock_notify_handler(struct notifier_block *this,
					unsigned long dock_event,
					void *unused)
{
	int ret = 0;
	/* plug in docking speaker+plug in device OR unplug one of them */
	u32 dock_plug_irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
					IRQF_SHARED;
	if (dock_event) {
		ret = gpio_request(msm8994_liquid_dock_dev->plug_gpio,
					   "dock-plug-det-irq");
		if (ret) {
			pr_err("%s:failed request msm8994_liquid_plug_gpio err = %d\n",
				__func__, ret);
			ret = -EINVAL;
			goto fail_gpio_irq;
		}

		msm8994_liquid_dock_dev->plug_det =
			gpio_get_value(msm8994_liquid_dock_dev->plug_gpio);
		msm8994_liquid_dock_dev->plug_irq =
			gpio_to_irq(msm8994_liquid_dock_dev->plug_gpio);

		ret = request_irq(msm8994_liquid_dock_dev->plug_irq,
				  msm8994_audio_plug_device_irq_handler,
				  dock_plug_irq_flags,
				  "liquid_dock_plug_irq",
				  msm8994_liquid_dock_dev);
		if (ret < 0) {
			pr_err("%s: Request Irq Failed err = %d\n",
				__func__, ret);
			goto fail_gpio_irq;
		}

		switch_set_state(&msm8994_liquid_dock_dev->audio_sdev,
				msm8994_liquid_dock_dev->plug_det);
		/*notify to audio deamon*/
		sysfs_notify(&msm8994_liquid_dock_dev->audio_sdev.dev->kobj,
				NULL, "state");
	} else {
		if (msm8994_liquid_dock_dev->plug_irq)
			free_irq(msm8994_liquid_dock_dev->plug_irq,
				 msm8994_liquid_dock_dev);

		if (msm8994_liquid_dock_dev->plug_gpio)
			gpio_free(msm8994_liquid_dock_dev->plug_gpio);
	}
	return NOTIFY_OK;

fail_gpio_irq:
	gpio_free(msm8994_liquid_dock_dev->plug_gpio);

fail_dock_gpio:
	return NOTIFY_DONE;
}


static struct notifier_block msm8994_liquid_docking_notifier = {
	.notifier_call  = msm8994_liquid_dock_notify_handler,
};

static int msm8994_liquid_init_docking(void)
{
	int ret = 0;
	int plug_gpio = 0;

	plug_gpio = of_get_named_gpio(spdev->dev.of_node,
					   "qcom,dock-plug-det-irq", 0);

	if (plug_gpio >= 0) {
		msm8994_liquid_dock_dev =
		 kzalloc(sizeof(*msm8994_liquid_dock_dev), GFP_KERNEL);
		if (!msm8994_liquid_dock_dev) {
			pr_err("msm8994_liquid_dock_dev alloc fail.\n");
			ret = -ENOMEM;
			goto exit;
		}

		msm8994_liquid_dock_dev->plug_gpio = plug_gpio;
		msm8994_liquid_dock_dev->audio_sdev.name =
						QC_AUDIO_EXTERNAL_SPK_1_EVENT;

		if (switch_dev_register(
			 &msm8994_liquid_dock_dev->audio_sdev) < 0) {
			pr_err("%s: dock device register in switch diretory failed\n",
				__func__);
			goto exit;
		}

		INIT_WORK(
			&msm8994_liquid_dock_dev->irq_work,
			msm8994_liquid_docking_irq_work);

		register_liquid_dock_notify(&msm8994_liquid_docking_notifier);
	}
	return 0;
exit:
	msm8994_liquid_dock_dev = NULL;
	return ret;
}

static void apq8094_db_device_irq_work(struct work_struct *work)
{
	struct audio_plug_dev *plug_dev =
		container_of(work, struct audio_plug_dev, irq_work);

	plug_dev->plug_det =
		gpio_get_value(plug_dev->plug_gpio);

	switch_set_state(&plug_dev->audio_sdev, !(plug_dev->plug_det));

	/*notify to audio deamon*/
	sysfs_notify(&plug_dev->audio_sdev.dev->kobj, NULL, "state");
}

static int apq8094_db_device_init(void)
{
	int ret = 0;
	int plug_gpio = 0;


	plug_gpio = of_get_named_gpio(spdev->dev.of_node,
					   "qcom,ext-spk-rear-panel-irq", 0);
	if (plug_gpio >= 0) {
		ret = msm8994_audio_plug_device_init(apq8094_db_ext_bp_out_dev,
						plug_gpio,
						"qcom,ext-spk-rear-panel-irq",
						QC_AUDIO_EXTERNAL_SPK_2_EVENT,
						&apq8094_db_device_irq_work);
		if (ret != 0) {
			pr_err("%s: external audio device init failed = %s\n",
				__func__,
				apq8094_db_ext_bp_out_dev->audio_sdev.name);
			return ret;
		}
	}

	plug_gpio = of_get_named_gpio(spdev->dev.of_node,
					   "qcom,ext-spk-front-panel-irq", 0);
	if (plug_gpio >= 0) {
		ret = msm8994_audio_plug_device_init(apq8094_db_ext_fp_out_dev,
						plug_gpio,
						"qcom,ext-spk-front-panel-irq",
						QC_AUDIO_EXTERNAL_SPK_1_EVENT,
						&apq8094_db_device_irq_work);
		if (ret != 0) {
			pr_err("%s: external audio device init failed = %s\n",
				__func__,
				apq8094_db_ext_fp_out_dev->audio_sdev.name);
			return ret;
		}
	}

	plug_gpio = of_get_named_gpio(spdev->dev.of_node,
					   "qcom,ext-mic-front-panel-irq", 0);
	if (plug_gpio >= 0) {
		ret = msm8994_audio_plug_device_init(apq8094_db_ext_fp_in_dev,
						plug_gpio,
						"qcom,ext-mic-front-panel-irq",
						QC_AUDIO_EXTERNAL_MIC_EVENT,
						&apq8094_db_device_irq_work);
		if (ret != 0)
			pr_err("%s: external audio device init failed = %s\n",
				__func__,
				apq8094_db_ext_fp_in_dev->audio_sdev.name);
	}

	return ret;
}

static void msm8994_ext_control(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	mutex_lock(&dapm->codec->mutex);
	pr_debug("%s: msm8994_spk_control = %d", __func__, msm8994_spk_control);
	if (msm8994_spk_control == MSM8994_SPK_ON) {
		snd_soc_dapm_enable_pin(dapm, "Lineout_1 amp");
		snd_soc_dapm_enable_pin(dapm, "Lineout_2 amp");
	} else {
		snd_soc_dapm_disable_pin(dapm, "Lineout_1 amp");
		snd_soc_dapm_disable_pin(dapm, "Lineout_2 amp");
	}
	mutex_unlock(&dapm->codec->mutex);
	snd_soc_dapm_sync(dapm);
}

static int msm8994_get_spk(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm8994_spk_control = %d\n",
			 __func__, msm8994_spk_control);
	ucontrol->value.integer.value[0] = msm8994_spk_control;
	return 0;
}

static int msm8994_set_spk(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	pr_debug("%s()\n", __func__);
	if (msm8994_spk_control == ucontrol->value.integer.value[0])
		return 0;

	msm8994_spk_control = ucontrol->value.integer.value[0];
	msm8994_ext_control(codec);
	return 1;
}

static int msm8994_ext_us_amp_init(void)
{
	int ret = 0;

	ext_us_amp_gpio = of_get_named_gpio(spdev->dev.of_node,
				"qcom,ext-ult-spk-amp-gpio", 0);
	if (ext_us_amp_gpio >= 0) {
		ret = gpio_request(ext_us_amp_gpio, "ext_us_amp_gpio");
		if (ret) {
			pr_err("%s: ext_us_amp_gpio request failed, ret:%d\n",
				__func__, ret);
			return ret;
		}
		gpio_direction_output(ext_us_amp_gpio, 0);
	}
	return ret;
}

static void msm8994_ext_us_amp_enable(u32 on)
{
	if (on)
		gpio_direction_output(ext_us_amp_gpio, 1);
	else
		gpio_direction_output(ext_us_amp_gpio, 0);

	pr_debug("%s: US Emitter GPIO enable:%s\n", __func__,
			on ? "Enable" : "Disable");
}

static int msm_ext_ultrasound_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *k, int event)
{
	pr_debug("%s()\n", __func__);
	if (!strcmp(w->name, "ultrasound amp")) {
		if (!gpio_is_valid(ext_us_amp_gpio)) {
			pr_err("%s: ext_us_amp_gpio isn't configured\n",
				__func__);
			return -EINVAL;
		}
		if (SND_SOC_DAPM_EVENT_ON(event))
			msm8994_ext_us_amp_enable(1);
		else
			msm8994_ext_us_amp_enable(0);
	} else {
		pr_err("%s() Invalid Widget = %s\n",
				__func__, w->name);
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_SND_USE_TERT_MI2S
static void msm8994_tert_mi2s_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pinctrl_info;
	int ret = 0;
	pr_info("%s: dai name %s %p  substream = %s  stream = %d  \n",
		__func__, cpu_dai->name, cpu_dai->dev,substream->name, substream->stream);

	if (atomic_dec_return(&tert_mi2s_rsc_ref) == 0) {
		tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_DISABLE;
	    tert_mi2s_clk.clk_val2 = Q6AFE_LPASS_OSR_CLK_DISABLE;
		tert_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_BOTH_VALID;
		tert_mi2s_clk.clk_src =Q6AFE_LPASS_CLK_SRC_INTERNAL;
		ret = afe_set_lpass_clock(AFE_PORT_ID_TERTIARY_MI2S_RX,	&tert_mi2s_clk);
		if (ret < 0)
			pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);

		ret = msm_set_pinctrl(pinctrl_info, INTF_TERT_MI2S, 0x0);

		if (ret)
			pr_err("%s: Reset pinctrl failed with %d\n", __func__, ret);

		 pr_info("%s Tertiary MI2S Clock is Disabled", __func__);
	}
}

static int msm8994_tert_mi2s_snd_startup(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_card *card = rtd->card;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pinctrl_info;

    pr_err("%s: dai name %s %p  substream = %s  stream = %d bit width =%d sample rate =%d  \n",
        __func__, cpu_dai->name, cpu_dai->dev,substream->name,
        substream->stream, tert_mi2s_bit_format, tert_mi2s_sample_rate);

	if (atomic_inc_return(&tert_mi2s_rsc_ref) == 1) {
        if (pinctrl_info == NULL) {
            pr_err("%s: pinctrl_info is NULL\n", __func__);
            ret = -EINVAL;
		    return ret;
        }
        if (pdata->tert_mux != NULL)
            iowrite32(I2S_PCM_SEL_I2S << I2S_PCM_SEL_OFFSET,
                    pdata->tert_mux);
        else
            pr_err("%s: TERT MI2S muxsel addr is NULL\n", __func__);

		ret = msm_set_pinctrl(pinctrl_info, INTF_TERT_MI2S, INTF_TERT_MI2S);

		if (ret) {
			pr_err("%s: MI2S TLMM pinctrl set failed with %d\n",
				 __func__, ret);
			return ret;
		}

        if((tert_mi2s_bit_format==32) || (tert_mi2s_bit_format==24)) {
			switch(tert_mi2s_sample_rate) {
				case SAMPLING_RATE_192KHZ:
					tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_12_P288_MHZ;
					break;
				case SAMPLING_RATE_96KHZ:
					tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_6_P144_MHZ;
					break;
				case SAMPLING_RATE_48KHZ:
				default:
					tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_3_P072_MHZ;
					break;
			}
		} else {
		    switch(tert_mi2s_sample_rate) {
				case SAMPLING_RATE_192KHZ:
					tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_6_P144_MHZ;
					break;
				case SAMPLING_RATE_96KHZ:
					tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_3_P072_MHZ;
					break;
				case SAMPLING_RATE_48KHZ:
				default:
					tert_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ;
					break;
			}
		}
        tert_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID; // bit clock1
        tert_mi2s_clk.clk_src =Q6AFE_LPASS_CLK_SRC_INTERNAL; // master

        ret = afe_set_lpass_clock(AFE_PORT_ID_TERTIARY_MI2S_RX, &tert_mi2s_clk);
    	if (ret < 0) {
    		dev_err(cpu_dai->dev,
    			"%s afe_set_lpass_clock failed\n", __func__);
    		goto err;
    	}
    	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBS_CFS);
    	if (ret < 0)
            pr_err("%s: set fmt cpu dai failed, err:%d\n", __func__, ret);
	}
err:
    return ret;
}

static int msm_be_tert_mi2s_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
	struct snd_pcm_hw_params *params)
{
    struct snd_interval *rate = hw_param_interval(params,
		SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
		SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *sample_bit = hw_param_interval(params,
		SNDRV_PCM_HW_PARAM_SAMPLE_BITS );
	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
		tert_mi2s_bit_format);
	rate->min = rate->max = tert_mi2s_sample_rate;
	channels->min = channels->max =2;
	if (tert_mi2s_bit_format == SNDRV_PCM_FORMAT_S16_LE)
		sample_bit->min = sample_bit->max = 16;
	else
		sample_bit->min = sample_bit->max = 24;
	pr_debug("%s Tert MI2S Sample Rate =%d, bit Format = %d \n", __func__, tert_mi2s_sample_rate, tert_mi2s_bit_format);
	return 0;
}

static struct snd_soc_ops msm8994_tert_mi2s_be_ops = {
	.startup = msm8994_tert_mi2s_snd_startup,
	.shutdown = msm8994_tert_mi2s_snd_shutdown
};
#endif /* CONFIG_SND_USE_TERT_MI2S */

#ifdef CONFIG_SND_USE_QUAT_MI2S
static void msm8994_quat_mi2s_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pinctrl_info;
	int ret = 0;

	pr_info("%s: dai name %s %p  substream = %s  stream = %d  \n",
		__func__, cpu_dai->name, cpu_dai->dev,substream->name, substream->stream);

	if (atomic_dec_return(&quat_mi2s_rsc_ref) == 0) {
		quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_DISABLE;
	    quat_mi2s_clk.clk_val2 = Q6AFE_LPASS_OSR_CLK_DISABLE;
		quat_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_BOTH_VALID;
		quat_mi2s_clk.clk_src =Q6AFE_LPASS_CLK_SRC_INTERNAL;
		ret = afe_set_lpass_clock(AFE_PORT_ID_QUATERNARY_MI2S_RX,	&quat_mi2s_clk);
		if (ret < 0)
			pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);

		ret = msm_set_pinctrl(pinctrl_info, INTF_QUAT_MI2S, 0x0);

		if (ret)
			pr_err("%s: Reset pinctrl failed with %d\n", __func__, ret);

		 pr_info("%s Quaternary MI2S Clock is Disabled", __func__);
	}
}

static int msm8994_quat_mi2s_snd_startup(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_card *card = rtd->card;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pinctrl_info;

    pr_info("%s: dai name %s %p  substream = %s  stream = %d bit width =%d sample rate =%d  \n",
        __func__, cpu_dai->name, cpu_dai->dev,substream->name,
        substream->stream, quat_mi2s_bit_format, quat_mi2s_sample_rate);

	if (atomic_inc_return(&quat_mi2s_rsc_ref) == 1) {
        if (pinctrl_info == NULL) {
            pr_err("%s: pinctrl_info is NULL\n", __func__);
            ret = -EINVAL;
		    return ret;
        }
        if (pdata->quat_mux != NULL)
            iowrite32(I2S_PCM_SEL_I2S << I2S_PCM_SEL_OFFSET,
                    pdata->quat_mux);
        else
            pr_err("%s: QUAT MI2S muxsel addr is NULL\n", __func__);

		ret = msm_set_pinctrl(pinctrl_info, INTF_QUAT_MI2S, INTF_QUAT_MI2S);

		if (ret) {
			pr_err("%s: MI2S TLMM pinctrl set failed with %d\n",
				   __func__, ret);
			return ret;
		}

        if(quat_mi2s_bit_format==SNDRV_PCM_FORMAT_S24_LE) {
			switch(quat_mi2s_sample_rate) {
				case SAMPLING_RATE_192KHZ:
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_12_P288_MHZ;
				case SAMPLING_RATE_96KHZ:
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_6_P144_MHZ;
				case SAMPLING_RATE_48KHZ:
				default:
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_3_P072_MHZ;
			}
		} else {
		    switch(quat_mi2s_sample_rate) {
				case SAMPLING_RATE_192KHZ:
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_6_P144_MHZ;
				case SAMPLING_RATE_96KHZ:
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_3_P072_MHZ;
				case SAMPLING_RATE_48KHZ:
				default:
					quat_mi2s_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ;
			}
		}
#ifdef MSM_QUAT_MI2S_MASTER
#ifdef MSM_QUAT_MI2S_MCLK
        quat_mi2s_clk.clk_val2 = Q6AFE_LPASS_OSR_CLK_12_P288_MHZ;
        quat_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_BOTH_VALID;
#else
        quat_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
#endif // MSM_QUAT_MI2S_MCLK
        quat_mi2s_clk.clk_src =Q6AFE_LPASS_CLK_SRC_INTERNAL;
#else
    	quat_mi2s_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
        quat_mi2s_clk.clk_src =Q6AFE_LPASS_CLK_SRC_EXTERNAL;
#endif  // MSM_QUAT_MI2S_MASTER
        ret = afe_set_lpass_clock(AFE_PORT_ID_QUATERNARY_MI2S_RX, &quat_mi2s_clk);
    	if (ret < 0) {
    		dev_err(cpu_dai->dev,
    			"%s afe_set_lpass_clock failed\n", __func__);
    		goto err;
    	}
    	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBS_CFS);
    	if (ret < 0)
            pr_err("%s: set fmt cpu dai failed, err:%d\n", __func__, ret);
	}
err:
    return ret;

}

#if defined(CONFIG_SND_SOC_TAS2552)
static int msm_be_quat_mi2s_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
	struct snd_pcm_hw_params *params)
{
    struct snd_interval *rate = hw_param_interval(params,
		SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
		SNDRV_PCM_HW_PARAM_CHANNELS);
	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
		quat_mi2s_bit_format);
	rate->min = rate->max = quat_mi2s_sample_rate;
	channels->min = channels->max =2;
	pr_debug("%s Quat MI2S Sample Rate =%d, bit Format = %d \n", __func__, quat_mi2s_sample_rate, quat_mi2s_bit_format);
	return 0;
}
#endif

static struct snd_soc_ops msm8994_quat_mi2s_be_ops = {
	.startup = msm8994_quat_mi2s_snd_startup,
	.shutdown = msm8994_quat_mi2s_snd_shutdown
};
#endif

static int msm_snd_enable_codec_ext_clk(struct snd_soc_codec *codec, int enable,
					bool dapm)
{
	int ret = 0;
	pr_debug("%s: enable = %d clk_users = %d\n",
		__func__, enable, clk_users);

	mutex_lock(&cdc_mclk_mutex);
	if (enable) {
		if (!codec_clk) {
			dev_err(codec->dev, "%s: did not get codec MCLK\n",
				__func__);
			ret = -EINVAL;
			goto exit;
		}
		clk_users++;
		if (clk_users != 1)
			goto exit;

		ret = clk_prepare_enable(codec_clk);
		if (ret) {
			pr_err("%s: clk_prepare failed, err:%d\n",
				__func__, ret);
			clk_users--;
			goto exit;
		}
		tomtom_mclk_enable(codec, 1, dapm);
	} else {
		if (clk_users > 0) {
			clk_users--;
			if (clk_users == 0) {
				tomtom_mclk_enable(codec, 0, dapm);
				clk_disable_unprepare(codec_clk);
			}
		} else {
			pr_err("%s: Error releasing codec MCLK\n", __func__);
			ret = -EINVAL;
			goto exit;
		}
	}
exit:
	mutex_unlock(&cdc_mclk_mutex);
	return ret;
}

static int msm8994_mclk_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	pr_debug("%s: event = %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return msm_snd_enable_codec_ext_clk(w->codec, 1, true);
	case SND_SOC_DAPM_POST_PMD:
		return msm_snd_enable_codec_ext_clk(w->codec, 0, true);
	}
	return 0;
}

static const struct snd_soc_dapm_widget msm8994_dapm_widgets[] = {

	SND_SOC_DAPM_SUPPLY("MCLK",  SND_SOC_NOPM, 0, 0,
	msm8994_mclk_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SPK("Lineout_1 amp", NULL),
	SND_SOC_DAPM_SPK("Lineout_3 amp", NULL),
	SND_SOC_DAPM_SPK("Lineout_2 amp", NULL),
	SND_SOC_DAPM_SPK("Lineout_4 amp", NULL),
	SND_SOC_DAPM_SPK("ultrasound amp", msm_ext_ultrasound_event),
	SND_SOC_DAPM_MIC("Handset Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("ANCRight Headset Mic", NULL),
	SND_SOC_DAPM_MIC("ANCLeft Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Analog Mic4", NULL),
	SND_SOC_DAPM_MIC("Analog Mic5", NULL),
	SND_SOC_DAPM_MIC("Analog Mic6", NULL),
	SND_SOC_DAPM_MIC("Analog Mic7", NULL),
	SND_SOC_DAPM_MIC("Analog Mic8", NULL),

	SND_SOC_DAPM_MIC("Handset SubMic", NULL),
	SND_SOC_DAPM_MIC("3rd Mic", NULL),

	SND_SOC_DAPM_MIC("Digital Mic1", NULL),
	SND_SOC_DAPM_MIC("Digital Mic2", NULL),
	SND_SOC_DAPM_MIC("Digital Mic3", NULL),
	SND_SOC_DAPM_MIC("Digital Mic4", NULL),
	SND_SOC_DAPM_MIC("Digital Mic5", NULL),
	SND_SOC_DAPM_MIC("Digital Mic6", NULL),
};

static int slim5_rx_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (slim5_rx_sample_rate) {
	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;

	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;

	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: slim5_rx_sample_rate = %d\n", __func__,
		 slim5_rx_sample_rate);

	return 0;
}

static int slim5_rx_sample_rate_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: ucontrol value = %ld\n", __func__,
		 ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim5_rx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		slim5_rx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
	default:
		slim5_rx_sample_rate = SAMPLING_RATE_48KHZ;
	}

	pr_debug("%s: slim5_rx_sample_rate = %d\n", __func__,
		 slim5_rx_sample_rate);

	return 0;
}

static int slim0_rx_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (slim0_rx_sample_rate) {
	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;

	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;

	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: slim0_rx_sample_rate = %d\n", __func__,
				slim0_rx_sample_rate);

	return 0;
}

static int slim0_rx_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: ucontrol value = %ld\n", __func__,
			ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim0_rx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		slim0_rx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
	default:
		slim0_rx_sample_rate = SAMPLING_RATE_48KHZ;
	}
#ifdef CONFIG_SND_USE_TERT_MI2S
	tert_mi2s_sample_rate = slim0_rx_sample_rate;
#endif
	pr_debug("%s: slim0_rx_sample_rate = %d\n", __func__,
			slim0_rx_sample_rate);

	return 0;
}

static int slim5_rx_bit_format_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{

	switch (slim5_rx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: slim5_rx_bit_format = %d, ucontrol value = %ld\n",
		 __func__, slim5_rx_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int slim5_rx_bit_format_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 1:
		slim5_rx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		slim5_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	return 0;
}

static int slim0_rx_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	switch (slim0_rx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_3LE:
		ucontrol->value.integer.value[0] = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: slim0_rx_bit_format = %d, ucontrol value = %ld\n",
			 __func__, slim0_rx_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int slim0_rx_bit_format_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim0_rx_bit_format = SNDRV_PCM_FORMAT_S24_3LE;
		break;
	case 1:
		slim0_rx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		slim0_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
#ifdef CONFIG_SND_USE_TERT_MI2S
	tert_mi2s_bit_format = slim0_rx_bit_format;
#endif
	return 0;
}

static int msm_slim_5_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_slim_5_rx_ch  = %d\n", __func__,
		 msm_slim_5_rx_ch);
	ucontrol->value.integer.value[0] = msm_slim_5_rx_ch - 1;
	return 0;
}

static int msm_slim_5_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_slim_5_rx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_slim_5_rx_ch = %d\n", __func__,
		 msm_slim_5_rx_ch);
	return 1;
}

static int msm_slim_0_rx_ch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_slim_0_rx_ch  = %d\n", __func__,
		 msm_slim_0_rx_ch);
	ucontrol->value.integer.value[0] = msm_slim_0_rx_ch - 1;
	return 0;
}

static int msm_slim_0_rx_ch_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	msm_slim_0_rx_ch = ucontrol->value.integer.value[0] + 1;

	pr_debug("%s: msm_slim_0_rx_ch = %d\n", __func__,
		 msm_slim_0_rx_ch);
	return 1;
}

static int slim0_tx_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (slim0_tx_sample_rate) {
	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;
	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;
	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: slim0_tx_sample_rate = %d\n", __func__,
				slim0_tx_sample_rate);

	return 0;
}

static int slim0_tx_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int rc = 0;

	pr_debug("%s: ucontrol value = %ld\n", __func__,
			ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim0_tx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		slim0_tx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
		slim0_tx_sample_rate = SAMPLING_RATE_48KHZ;
		break;
	default:
		rc = -EINVAL;
		pr_err("%s: invalid sample rate being passed\n", __func__);
		break;
	}

	pr_debug("%s: slim0_tx_sample_rate = %d\n", __func__,
			slim0_tx_sample_rate);

	return rc;
}

static int slim0_tx_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	switch (slim0_tx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: slim0_tx_bit_format = %d, ucontrol value = %ld\n",
			 __func__, slim0_tx_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int slim0_tx_bit_format_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	int rc = 0;

	switch (ucontrol->value.integer.value[0]) {
	case 1:
		slim0_tx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
		slim0_tx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	default:
		pr_err("%s: invalid value %ld\n", __func__,
		       ucontrol->value.integer.value[0]);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int msm_slim_0_tx_ch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_slim_0_tx_ch  = %d\n", __func__,
		 msm_slim_0_tx_ch);
	ucontrol->value.integer.value[0] = msm_slim_0_tx_ch - 1;
	return 0;
}

static int msm_slim_0_tx_ch_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	msm_slim_0_tx_ch = ucontrol->value.integer.value[0] + 1;

	pr_debug("%s: msm_slim_0_tx_ch = %d\n", __func__, msm_slim_0_tx_ch);
	return 1;
}

static int msm_vi_feed_tx_ch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = (msm_vi_feed_tx_ch/2 - 1);
	pr_debug("%s: msm_vi_feed_tx_ch = %ld\n", __func__,
		 ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_vi_feed_tx_ch_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	msm_vi_feed_tx_ch =
		roundup_pow_of_two(ucontrol->value.integer.value[0] + 2);

	pr_debug("%s: msm_vi_feed_tx_ch = %d\n", __func__, msm_vi_feed_tx_ch);
	return 1;
}

static int msm_btsco_rate_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_btsco_rate  = %d", __func__, msm_btsco_rate);
	ucontrol->value.integer.value[0] = msm_btsco_rate;
	return 0;
}

static int msm_btsco_rate_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case SAMPLING_RATE_8KHZ:
		msm_btsco_rate = SAMPLING_RATE_8KHZ;
		break;
	case SAMPLING_RATE_16KHZ:
		msm_btsco_rate = SAMPLING_RATE_16KHZ;
		break;
	default:
		msm_btsco_rate = SAMPLING_RATE_8KHZ;
		break;
	}
	pr_debug("%s: msm_btsco_rate = %d\n", __func__, msm_btsco_rate);
	return 0;
}

static int hdmi_rx_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	switch (hdmi_rx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: hdmi_rx_bit_format = %d, ucontrol value = %ld\n",
			 __func__, hdmi_rx_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int hdmi_rx_bit_format_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 1:
		hdmi_rx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		hdmi_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	pr_debug("%s: hdmi_rx_bit_format = %d, ucontrol value = %ld\n",
			 __func__, hdmi_rx_bit_format,
			ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_hdmi_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_hdmi_rx_ch  = %d\n", __func__,
			msm_hdmi_rx_ch);
	ucontrol->value.integer.value[0] = msm_hdmi_rx_ch - 2;

	return 0;
}

static int msm_hdmi_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_hdmi_rx_ch = ucontrol->value.integer.value[0] + 2;
	if (msm_hdmi_rx_ch > 8) {
		pr_err("%s: channels %d exceeded 8.Limiting to max chs-8\n",
			__func__, msm_hdmi_rx_ch);
		msm_hdmi_rx_ch = 8;
	}
	pr_debug("%s: msm_hdmi_rx_ch = %d\n", __func__, msm_hdmi_rx_ch);

	return 1;
}

static int hdmi_rx_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (hdmi_rx_sample_rate) {
	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;

	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;

	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: hdmi_rx_sample_rate = %d\n", __func__,
				hdmi_rx_sample_rate);

	return 0;
}

static int hdmi_rx_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: ucontrol value = %ld\n", __func__,
			ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		hdmi_rx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		hdmi_rx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
	default:
		hdmi_rx_sample_rate = SAMPLING_RATE_48KHZ;
	}

	pr_debug("%s: hdmi_rx_sample_rate = %d\n", __func__,
			hdmi_rx_sample_rate);

	return 0;
}

static int msm8994_auxpcm_rate_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = msm8994_auxpcm_rate;
	return 0;
}

static int msm8994_auxpcm_rate_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 0:
		msm8994_auxpcm_rate = SAMPLING_RATE_8KHZ;
		break;
	case 1:
		msm8994_auxpcm_rate = SAMPLING_RATE_16KHZ;
		break;
	default:
		msm8994_auxpcm_rate = SAMPLING_RATE_8KHZ;
		break;
	}
	return 0;
}

static int msm_proxy_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_proxy_rx_ch = %d\n", __func__,
						msm_proxy_rx_ch);
	ucontrol->value.integer.value[0] = msm_proxy_rx_ch - 1;
	return 0;
}

static int msm_proxy_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_proxy_rx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_proxy_rx_ch = %d\n", __func__,
						msm_proxy_rx_ch);
	return 1;
}

static int msm_auxpcm_be_params_fixup(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = msm8994_auxpcm_rate;
	channels->min = channels->max = 1;

	return 0;
}

static int msm_proxy_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s: msm_proxy_rx_ch =%d\n", __func__, msm_proxy_rx_ch);

	if (channels->max < 2)
		channels->min = channels->max = 2;
	channels->min = channels->max = msm_proxy_rx_ch;
	rate->min = rate->max = 48000;
	return 0;
}

static int msm_proxy_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	rate->min = rate->max = 48000;
	return 0;
}

static int msm8994_hdmi_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s channels->min %u channels->max %u ()\n", __func__,
			channels->min, channels->max);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				hdmi_rx_bit_format);
	if (channels->max < 2)
		channels->min = channels->max = 2;
	rate->min = rate->max = hdmi_rx_sample_rate;
	channels->min = channels->max = msm_hdmi_rx_ch;

	return 0;
}

static int msm_set_pinctrl(struct msm_pinctrl_info *pinctrl_info,
	u16 state_mask,	u16 intf_enable)
{
	int ret = 0;
	u16 curr_pinctrl = 0;
	u16 new_pinctrl = 0;

	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	curr_pinctrl = pinctrl_info->pinctrl_state;
	new_pinctrl = (curr_pinctrl & ~ state_mask) | (intf_enable & state_mask);


	pinctrl_info->pinctrl_state = new_pinctrl;
	pr_debug("%s:curr_state = %d new_state = %d\n", __func__,curr_pinctrl, new_pinctrl);


	if (curr_pinctrl == new_pinctrl) {
		pr_debug("%s: Already in same state\n", __func__);
		goto err;
	}

    pr_debug("%s, %s, %p\n", __func__, pin_states[new_pinctrl], (void*)pinctrl_info->pctrl[new_pinctrl]);

	ret = pinctrl_select_state(pinctrl_info->pinctrl,
		pinctrl_info->pctrl[new_pinctrl]);

	if (ret) {
		pr_err("%s: pinctrl_info->pctrl[%d] failed with  %d\n",__func__,new_pinctrl, ret);
		ret = -EIO;
		goto err;
	}
err:
	return ret;
}

static void msm_release_pinctrl(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pinctrl_info;

	if (pinctrl_info) {
		iounmap(pdata->pri_mux);
		iounmap(pdata->sec_mux);
#ifdef CONFIG_SND_USE_TERT_MI2S
		iounmap(pdata->tert_mux);
#endif
#ifdef CONFIG_SND_USE_QUAT_MI2S
		iounmap(pdata->quat_mux);
#endif // CONFIG_SND_USE_QUAT_MI2S
		devm_pinctrl_put(pinctrl_info->pinctrl);
		pinctrl_info->pinctrl = NULL;
	}
}

static int msm_get_pinctrl(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = NULL;
	struct pinctrl *pinctrl;
	struct resource	*muxsel;
	int ret;
	int i;

	pinctrl_info = &pdata->pinctrl_info;

	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		return -EINVAL;
	}

	pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(pinctrl)) {
		pr_err("%s: Unable to get pinctrl handle\n", __func__);
		return -EINVAL;
	}
	pinctrl_info->pinctrl = pinctrl;

	/* get all the states handles from Device Tree */
	for (i = 0 ;  i  < MAX_PINCTRL_STATE ; i++){
#if defined(CONFIG_MACH_MSM8992_PPLUS) //for evb_3, temp code
				if(lge_get_board_revno() < HW_REV_0 && i > 7)
					break;
#endif
		pinctrl_info->pctrl[i] = pinctrl_lookup_state(pinctrl, pin_states[i]);
        pr_debug("%s, %s, %p\n", __func__, pin_states[i], (void*)pinctrl_info->pctrl[i]);
		if (IS_ERR(pinctrl_info->pctrl[i])) {
			pr_err("%s: could not get %s \n", __func__,pin_states[i]);
			goto err;
		}
	}


	/* Reset the TLMM pins to a default state */
	ret = pinctrl_select_state(pinctrl_info->pinctrl,
			pinctrl_info->pctrl[INTF_NONE]);

	if (ret != 0) {
		pr_err("%s: Disable TLMM pins failed with %d\n",
			__func__, ret);
		ret = -EIO;
		goto err;
	}
	pinctrl_info->pinctrl_state = INTF_NONE;

	muxsel = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"lpaif_pri_mode_muxsel");
	if (!muxsel) {
		dev_err(&pdev->dev, "MUX addr invalid for MI2S\n");
		ret = -ENODEV;
		goto err;
	}
	pdata->pri_mux = ioremap(muxsel->start, resource_size(muxsel));
	if (pdata->pri_mux == NULL) {
		pr_err("%s: MI2S muxsel virt addr is null\n", __func__);
		ret = -EINVAL;
		goto err;
	}
	muxsel = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"lpaif_sec_mode_muxsel");
	if (!muxsel) {
		dev_err(&pdev->dev, "MUX addr invalid for AUXPCM\n");
		ret = -ENODEV;
		goto err;
	}
	pdata->sec_mux = ioremap(muxsel->start, resource_size(muxsel));
	if (pdata->sec_mux == NULL) {
		pr_err("%s: AUXPCM muxsel virt addr is null\n", __func__);
		ret = -EINVAL;
		goto err;
	}

#ifdef CONFIG_SND_USE_QUAT_MI2S
    muxsel = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"lpaif_quat_mode_muxsel");
	if (!muxsel) {
		dev_err(&pdev->dev, "MUX addr invalid for QUAT MI2S\n");
		ret = -ENODEV;
		goto err;
	}
	pdata->quat_mux = ioremap(muxsel->start, resource_size(muxsel));
	if (pdata->quat_mux == NULL) {
		pr_err("%s: QUAT muxsel virt addr is null\n", __func__);
		ret = -EINVAL;
		goto err;
	}
#endif // CONFIG_SND_USE_QUAT_MI2S

#ifdef CONFIG_SND_USE_TERT_MI2S
    muxsel = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"lpaif_tert_mode_muxsel");
	if (!muxsel) {
		dev_err(&pdev->dev, "MUX addr invalid for TERT MI2S\n");
		ret = -ENODEV;
		goto err;
	}
	pdata->tert_mux = ioremap(muxsel->start, resource_size(muxsel));
	if (pdata->tert_mux == NULL) {
		pr_err("%s: TERT muxsel virt addr is null\n", __func__);
		ret = -EINVAL;
		goto err;
	}
#endif /* CONFIG_SND_USE_TERT_MI2S */
	return 0;

err:
    pr_err("%s: enter to label, err.\n", __func__);
	if (pdata->pri_mux)
		iounmap(pdata->pri_mux);
	devm_pinctrl_put(pinctrl);
	pinctrl_info->pinctrl = NULL;
	return -EINVAL;
}

static int msm_sec_auxpcm_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pinctrl_info;
	int ret = 0;
	u32 pcm_sel_reg = 0;

	pr_debug("%s(): substream = %s, sec_auxpcm_rsc_ref counter = %d\n",
		__func__, substream->name, atomic_read(&sec_auxpcm_rsc_ref));

	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		ret = -EINVAL;
		goto err;
	}
	if (atomic_inc_return(&sec_auxpcm_rsc_ref) == 1) {
		if (pdata->sec_mux != NULL) {
			pcm_sel_reg = ioread32(pdata->sec_mux);
			iowrite32(I2S_PCM_SEL_PCM << I2S_PCM_SEL_OFFSET,
				  pdata->sec_mux);
		} else {
			pr_err("%s Sec AUXPCM MUX addr is NULL\n", __func__);
			ret = -EINVAL;
			goto err;
		}
		ret = msm_set_pinctrl(pinctrl_info, INTF_SEC_AUXPCM,INTF_SEC_AUXPCM);
		if (ret) {
			pr_err("%s: AUXPCM TLMM pinctrl set failed with %d\n",
				__func__, ret);
			return ret;
		}
	}
err:
	return ret;
}

static void msm_sec_auxpcm_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pinctrl_info;
	int ret = 0;

	pr_debug("%s(): substream = %s, sec_auxpcm_rsc_ref counter = %d\n",
		__func__, substream->name, atomic_read(&sec_auxpcm_rsc_ref));

	if (atomic_dec_return(&sec_auxpcm_rsc_ref) == 0) {
		ret = msm_set_pinctrl(pinctrl_info,INTF_SEC_AUXPCM,0x0);
		if (ret)
			pr_err("%s Reset pinctrl failed with %d\n",
				__func__, ret);
	}
}

static struct snd_soc_ops msm_sec_auxpcm_be_ops = {
	.startup = msm_sec_auxpcm_startup,
	.shutdown = msm_sec_auxpcm_shutdown,
};

static int msm_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s: channel:%d\n", __func__, msm_pri_mi2s_tx_ch);
	rate->min = rate->max = SAMPLING_RATE_48KHZ;
	channels->min = channels->max = msm_pri_mi2s_tx_ch;
	return 0;
}

static int msm8994_mi2s_snd_startup(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pinctrl_info;

	pr_debug("%s: substream = %s  stream = %d\n", __func__,
		substream->name, substream->stream);

	if (pinctrl_info == NULL) {
		pr_err("%s: pinctrl_info is NULL\n", __func__);
		ret = -EINVAL;
		goto err;
	}
	if (pdata->pri_mux != NULL)
		iowrite32(I2S_PCM_SEL_I2S << I2S_PCM_SEL_OFFSET,
				pdata->pri_mux);
	else
		pr_err("%s: MI2S muxsel addr is NULL\n", __func__);

	ret = msm_set_pinctrl(pinctrl_info, INTF_PRI_MI2S,INTF_PRI_MI2S);
	if (ret) {
		pr_err("%s: MI2S TLMM pinctrl set failed with %d\n",
			__func__, ret);
		return ret;
	}
	mi2s_tx_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ;
	mi2s_tx_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
	ret = afe_set_lpass_clock(AFE_PORT_ID_PRIMARY_MI2S_TX,
				&mi2s_tx_clk);
	if (ret < 0) {
		pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);
		goto err;
	}
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		pr_err("%s: set fmt cpu dai failed, err:%d\n", __func__, ret);
err:
	return ret;
}

static void msm8994_mi2s_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct msm_pinctrl_info *pinctrl_info = &pdata->pinctrl_info;
	int ret = 0;

	pr_debug("%s: substream = %s  stream = %d\n", __func__,
		substream->name, substream->stream);

	mi2s_tx_clk.clk_val1 = Q6AFE_LPASS_IBIT_CLK_DISABLE;
	mi2s_tx_clk.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
	ret = afe_set_lpass_clock(AFE_PORT_ID_PRIMARY_MI2S_TX,
				&mi2s_tx_clk);
	if (ret < 0)
		pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);

	ret = msm_set_pinctrl(pinctrl_info, INTF_PRI_MI2S,0x0);
	if (ret)
		pr_err("%s: Reset pinctrl failed with %d\n",
			__func__, ret);
}

static struct snd_soc_ops msm8994_mi2s_be_ops = {
	.startup = msm8994_mi2s_snd_startup,
	.shutdown = msm8994_mi2s_snd_shutdown,
};

static int msm_slim_5_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   slim5_rx_bit_format);
	rate->min = rate->max = slim5_rx_sample_rate;
	channels->min = channels->max = msm_slim_5_rx_ch;

	 pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		  __func__, params_format(params), params_rate(params),
		  msm_slim_5_rx_ch);

	return 0;
}

static int msm_slim_0_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
	SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s()\n", __func__);
	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   slim0_rx_bit_format);
	rate->min = rate->max = slim0_rx_sample_rate;
	channels->min = channels->max = msm_slim_0_rx_ch;

	 pr_debug("%s: format = %d, rate = %d, channels = %d\n",
			  __func__, params_format(params), params_rate(params),
			  msm_slim_0_rx_ch);

	return 0;
}

static int msm_slim_0_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
	SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s()\n", __func__);
	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   slim0_tx_bit_format);
	rate->min = rate->max = slim0_tx_sample_rate;
	channels->min = channels->max = msm_slim_0_tx_ch;

	return 0;
}

static int msm_slim_4_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
	SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = 48000;
	channels->min = channels->max = msm_vi_feed_tx_ch;
	pr_debug("%s: %d\n", __func__, msm_vi_feed_tx_ch);
	return 0;
}

static int msm_slim_5_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	void *config;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_interval *rate =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	int rc = 0;

	pr_debug("%s: enter\n", __func__);
	rate->min = rate->max = 16000;
	channels->min = channels->max = 1;

	config = tomtom_get_afe_config(codec, AFE_SLIMBUS_SLAVE_PORT_CONFIG);
	rc = afe_set_config(AFE_SLIMBUS_SLAVE_PORT_CONFIG, config,
			    SLIMBUS_5_TX);
	if (rc) {
		pr_err("%s: Failed to set slimbus slave port config %d\n",
		       __func__, rc);
		return rc;
	}

	return rc;
}

static int msm_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	pr_debug("%s:\n", __func__);
	rate->min = rate->max = 48000;
	return 0;
}

static int msm_be_fm_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s:\n", __func__);
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	return 0;
}

static const struct soc_enum msm_snd_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, spk_function),
	SOC_ENUM_SINGLE_EXT(2, slim0_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(8, slim0_tx_ch_text),
	SOC_ENUM_SINGLE_EXT(7, hdmi_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rx_bit_format_text), rx_bit_format_text),
	SOC_ENUM_SINGLE_EXT(3, slim0_rx_sample_rate_text),
	SOC_ENUM_SINGLE_EXT(8, proxy_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(3, hdmi_rx_sample_rate_text),
	SOC_ENUM_SINGLE_EXT(2, vi_feed_ch_text),
	SOC_ENUM_SINGLE_EXT(3, slim5_rx_sample_rate_text),
	SOC_ENUM_SINGLE_EXT(2, slim5_rx_bit_format_text),
	SOC_ENUM_SINGLE_EXT(2, slim5_rx_ch_text),
};

static const struct snd_kcontrol_new msm_snd_controls[] = {
	SOC_ENUM_EXT("Speaker Function", msm_snd_enum[0], msm8994_get_spk,
			msm8994_set_spk),
	SOC_ENUM_EXT("SLIM_0_RX Channels", msm_snd_enum[1],
			msm_slim_0_rx_ch_get, msm_slim_0_rx_ch_put),
	SOC_ENUM_EXT("SLIM_5_RX Channels", msm_snd_enum[11],
			msm_slim_5_rx_ch_get, msm_slim_5_rx_ch_put),
	SOC_ENUM_EXT("SLIM_0_TX Channels", msm_snd_enum[2],
			msm_slim_0_tx_ch_get, msm_slim_0_tx_ch_put),
	SOC_ENUM_EXT("VI_FEED_TX Channels", msm_snd_enum[8],
			msm_vi_feed_tx_ch_get, msm_vi_feed_tx_ch_put),
	SOC_ENUM_EXT("AUX PCM SampleRate", msm8994_auxpcm_enum[0],
			msm8994_auxpcm_rate_get, msm8994_auxpcm_rate_put),
	SOC_ENUM_EXT("HDMI_RX Channels", msm_snd_enum[3],
			msm_hdmi_rx_ch_get, msm_hdmi_rx_ch_put),
	SOC_ENUM_EXT("SLIM_0_RX Format", msm_snd_enum[4],
			slim0_rx_bit_format_get, slim0_rx_bit_format_put),
	SOC_ENUM_EXT("SLIM_5_RX Format", msm_snd_enum[10],
			slim5_rx_bit_format_get, slim5_rx_bit_format_put),
	SOC_ENUM_EXT("SLIM_0_RX SampleRate", msm_snd_enum[5],
			slim0_rx_sample_rate_get, slim0_rx_sample_rate_put),
	SOC_ENUM_EXT("SLIM_5_RX SampleRate", msm_snd_enum[9],
			slim5_rx_sample_rate_get, slim5_rx_sample_rate_put),
	SOC_ENUM_EXT("HDMI_RX Bit Format", msm_snd_enum[4],
			hdmi_rx_bit_format_get, hdmi_rx_bit_format_put),
	SOC_ENUM_EXT("PROXY_RX Channels", msm_snd_enum[6],
			msm_proxy_rx_ch_get, msm_proxy_rx_ch_put),
	SOC_ENUM_EXT("Internal BTSCO SampleRate", msm_btsco_enum[0],
		     msm_btsco_rate_get, msm_btsco_rate_put),
	SOC_ENUM_EXT("HDMI_RX SampleRate", msm_snd_enum[7],
			hdmi_rx_sample_rate_get, hdmi_rx_sample_rate_put),
	SOC_ENUM_EXT("SLIM_0_TX Format", msm_snd_enum[4],
			slim0_tx_bit_format_get, slim0_tx_bit_format_put),
	SOC_ENUM_EXT("SLIM_0_TX SampleRate", msm_snd_enum[5],
			slim0_tx_sample_rate_get, slim0_tx_sample_rate_put),
};

static bool msm8994_swap_gnd_mic(struct snd_soc_codec *codec)
{
	struct snd_soc_card *card = codec->card;
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	int value = gpio_get_value_cansleep(pdata->us_euro_gpio);
	pr_debug("%s: swap select switch %d to %d\n", __func__, value, !value);
	gpio_set_value_cansleep(pdata->us_euro_gpio, !value);
	return true;
}

static int msm_afe_set_config(struct snd_soc_codec *codec)
{
	int rc;
	void *config_data;

	pr_debug("%s: enter\n", __func__);
	config_data = tomtom_get_afe_config(codec, AFE_CDC_REGISTERS_CONFIG);
	rc = afe_set_config(AFE_CDC_REGISTERS_CONFIG, config_data, 0);
	if (rc) {
		pr_err("%s: Failed to set codec registers config %d\n",
		       __func__, rc);
		return rc;
	}

	config_data = tomtom_get_afe_config(codec, AFE_SLIMBUS_SLAVE_CONFIG);
	rc = afe_set_config(AFE_SLIMBUS_SLAVE_CONFIG, config_data, 0);
	if (rc) {
		pr_err("%s: Failed to set slimbus slave config %d\n", __func__,
		       rc);
		return rc;
	}

	return 0;
}

static void msm_afe_clear_config(void)
{
	afe_clear_config(AFE_CDC_REGISTERS_CONFIG);
	afe_clear_config(AFE_SLIMBUS_SLAVE_CONFIG);
}

static int  msm8994_adsp_state_callback(struct notifier_block *nb,
		unsigned long value, void *priv)
{
	if (value == SUBSYS_BEFORE_SHUTDOWN) {
		pr_debug("%s: ADSP is about to shutdown. Clearing AFE config\n",
			 __func__);
		msm_afe_clear_config();
	} else if (value == SUBSYS_AFTER_POWERUP) {
		pr_debug("%s: ADSP is up\n", __func__);
	}

	return NOTIFY_OK;
}

static struct notifier_block adsp_state_notifier_block = {
	.notifier_call = msm8994_adsp_state_callback,
	.priority = -INT_MAX,
};

static int msm8994_wcd93xx_codec_up(struct snd_soc_codec *codec)
{
	int err;
	unsigned long timeout;
	int adsp_ready = 0;

	timeout = jiffies +
		msecs_to_jiffies(ADSP_STATE_READY_TIMEOUT_MS);

	do {
		if (!q6core_is_adsp_ready()) {
			pr_err("%s: ADSP Audio isn't ready\n", __func__);
		} else {
			pr_debug("%s: ADSP Audio is ready\n", __func__);
			adsp_ready = 1;
			break;
		}
	} while (time_after(timeout, jiffies));

	if (!adsp_ready) {
		pr_err("%s: timed out waiting for ADSP Audio\n", __func__);
		return -ETIMEDOUT;
	}

	err = msm_afe_set_config(codec);
	if (err)
		pr_err("%s: Failed to set AFE config. err %d\n",
				__func__, err);
	return err;
}

static int msm8994_codec_event_cb(struct snd_soc_codec *codec,
		enum wcd9xxx_codec_event codec_event)
{
	switch (codec_event) {
	case WCD9XXX_CODEC_EVENT_CODEC_UP:
		return msm8994_wcd93xx_codec_up(codec);
	default:
		pr_err("%s: UnSupported codec event %d\n",
				__func__, codec_event);
		return -EINVAL;
	}
}

static int msm_snd_get_ext_clk_cnt(void)
{
	return clk_users;
}

static int msm_audrx_init(struct snd_soc_pcm_runtime *rtd)
{
    int ret;
	int err;
	void *config_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	/* Codec SLIMBUS configuration
	 * RX1, RX2, RX3, RX4, RX5, RX6, RX7, RX8, RX9, RX10, RX11, RX12, RX13
	 * TX1, TX2, TX3, TX4, TX5, TX6, TX7, TX8, TX9, TX10, TX11, TX12, TX13
	 * TX14, TX15, TX16
	 */
	unsigned int rx_ch[TOMTOM_RX_MAX] = {144, 145, 146, 147, 148, 149, 150,
					    151, 152, 153, 154, 155, 156};
	unsigned int tx_ch[TOMTOM_TX_MAX]  = {128, 129, 130, 131, 132, 133,
					     134, 135, 136, 137, 138, 139,
					     140, 141, 142, 143};

	pr_info("%s: dev_name%s\n", __func__, dev_name(cpu_dai->dev));

	rtd->pmdown_time = 0;

	err = snd_soc_add_codec_controls(codec, msm_snd_controls,
					 ARRAY_SIZE(msm_snd_controls));
	if (err < 0) {
		pr_err("%s: add_codec_controls failed, err%d\n",
			__func__, err);
		return err;
	}

	err = msm8994_liquid_init_docking();
	if (err) {
		pr_err("%s: 8994 init Docking stat IRQ failed (%d)\n",
			   __func__, err);
		return err;
	}

	err = msm8994_ext_us_amp_init();
	if (err) {
		pr_err("%s: MTP 8994 US Emitter GPIO init failed (%d)\n",
			__func__, err);
		return err;
	}

	snd_soc_dapm_new_controls(dapm, msm8994_dapm_widgets,
				ARRAY_SIZE(msm8994_dapm_widgets));

	snd_soc_dapm_enable_pin(dapm, "Lineout_1 amp");
	snd_soc_dapm_enable_pin(dapm, "Lineout_3 amp");
	snd_soc_dapm_enable_pin(dapm, "Lineout_2 amp");
	snd_soc_dapm_enable_pin(dapm, "Lineout_4 amp");

	snd_soc_dapm_ignore_suspend(dapm, "Lineout_1 amp");
	snd_soc_dapm_ignore_suspend(dapm, "Lineout_3 amp");
	snd_soc_dapm_ignore_suspend(dapm, "Lineout_2 amp");
	snd_soc_dapm_ignore_suspend(dapm, "Lineout_4 amp");
	snd_soc_dapm_ignore_suspend(dapm, "ultrasound amp");
	snd_soc_dapm_ignore_suspend(dapm, "Handset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "Headset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "ANCRight Headset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "ANCLeft Headset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic1");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic2");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic3");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic4");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic5");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic6");

	snd_soc_dapm_ignore_suspend(dapm, "MADINPUT");
	snd_soc_dapm_ignore_suspend(dapm, "MAD_CPE_INPUT");
	snd_soc_dapm_ignore_suspend(dapm, "AIF4 MAD");
	snd_soc_dapm_ignore_suspend(dapm, "AIF4 VI"); //NEED TO CHECK FOR SPEAKER PROTECTION
    snd_soc_dapm_ignore_suspend(dapm, "VIINPUT"); //NEED TO CHECK FOR SPEAKER PROTECTION
	snd_soc_dapm_ignore_suspend(dapm, "EAR");
	snd_soc_dapm_ignore_suspend(dapm, "HEADPHONE");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT1");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT2");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT3");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT4");
	snd_soc_dapm_ignore_suspend(dapm, "SPK_OUT");
	snd_soc_dapm_ignore_suspend(dapm, "ANC HEADPHONE");
	snd_soc_dapm_ignore_suspend(dapm, "ANC EAR");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC1");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC2");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC3");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC4");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC5");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC6");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC1");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC2");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC3");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC4");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC5");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC6");
	snd_soc_dapm_ignore_suspend(dapm, "AIF4 VI");
	snd_soc_dapm_ignore_suspend(dapm, "VIINPUT");

	snd_soc_dapm_sync(dapm);

	codec_clk = clk_get(&spdev->dev, "osr_clk");
	if (IS_ERR(codec_clk)) {
		pr_err("%s: error clk_get %lu\n",
			__func__, PTR_ERR(codec_clk));
		return -EINVAL;
	}

	snd_soc_dai_set_channel_map(codec_dai, ARRAY_SIZE(tx_ch),
				    tx_ch, ARRAY_SIZE(rx_ch), rx_ch);

	err = msm_afe_set_config(codec);
	if (err) {
		pr_err("%s: Failed to set AFE config %d\n", __func__, err);
		goto out;
	}

	config_data = tomtom_get_afe_config(codec, AFE_AANC_VERSION);
	err = afe_set_config(AFE_AANC_VERSION, config_data, 0);
	if (err) {
		pr_err("%s: Failed to set aanc version %d\n",
			__func__, err);
		goto out;
	}
	config_data = tomtom_get_afe_config(codec,
				AFE_CDC_CLIP_REGISTERS_CONFIG);
	if (config_data) {
		err = afe_set_config(AFE_CDC_CLIP_REGISTERS_CONFIG,
					config_data, 0);
		if (err) {
			pr_err("%s: Failed to set clip registers %d\n",
				__func__, err);
			goto out;
		}
	}
	config_data = tomtom_get_afe_config(codec, AFE_CLIP_BANK_SEL);
	if (config_data) {
		err = afe_set_config(AFE_CLIP_BANK_SEL, config_data, 0);
		if (err) {
			pr_err("%s: Failed to set AFE bank selection %d\n",
				__func__, err);
			goto out;
		}
	}

	/* start mbhc */
	mbhc_cfg.calibration = def_codec_mbhc_cal();
	if (mbhc_cfg.calibration) {
		err = tomtom_hs_detect(codec, &mbhc_cfg);
		if (err) {
			pr_err("%s: tomtom_hs_detect failed, err:%d\n",
				__func__, err);
			goto out;
		}
	} else {
		pr_err("%s: mbhc_cfg calibration is NULL\n", __func__);
		err = -ENOMEM;
		goto out;
	}

	adsp_state_notifier =
	    subsys_notif_register_notifier("adsp",
					   &adsp_state_notifier_block);
	if (!adsp_state_notifier) {
		pr_err("%s: Failed to register adsp state notifier\n",
		       __func__);
		err = -EFAULT;
		tomtom_hs_detect_exit(codec);
		goto out;
	}

	tomtom_event_register(msm8994_codec_event_cb, rtd->codec);
	tomtom_register_ext_clk_cb(msm_snd_enable_codec_ext_clk,
				   msm_snd_get_ext_clk_cnt,
				   rtd->codec);

	err = msm_snd_enable_codec_ext_clk(rtd->codec, 1, false);
	if (IS_ERR_VALUE(err)) {
		pr_err("%s: Failed to enable mclk, err = 0x%x\n",
			__func__, err);
		goto out;
	}
    ret = tomtom_enable_qfuse_sensing(rtd->codec);

    if(ret)
        panic("[audio bsp] tomtom_enable_qfuse_sensing failed\n");
	err = msm_snd_enable_codec_ext_clk(rtd->codec, 0, false);
	if (IS_ERR_VALUE(err)) {
		pr_err("%s: Failed to disable mclk, err = 0x%x\n",
			__func__, err);
		goto out;
	}

	return 0;
out:
	clk_put(codec_clk);
	return err;
}

static void *def_codec_mbhc_cal(void)
{
	void *codec_cal;
	struct wcd9xxx_mbhc_btn_detect_cfg *btn_cfg;
	u16 *btn_low, *btn_high;
	u8 *n_ready, *n_cic, *gain;

	codec_cal = kzalloc(WCD9XXX_MBHC_CAL_SIZE(WCD9XXX_MBHC_DEF_BUTTONS,
						WCD9XXX_MBHC_DEF_RLOADS),
			    GFP_KERNEL);
	if (!codec_cal) {
		pr_err("%s: out of memory\n", __func__);
		return NULL;
	}

#define S(X, Y) ((WCD9XXX_MBHC_CAL_GENERAL_PTR(codec_cal)->X) = (Y))
	S(t_ldoh, 100);
	S(t_bg_fast_settle, 100);
	S(t_shutdown_plug_rem, 255);
	S(mbhc_nsa, 4);
	S(mbhc_navg, 4);
#undef S
#define S(X, Y) ((WCD9XXX_MBHC_CAL_PLUG_DET_PTR(codec_cal)->X) = (Y))
	S(mic_current, TOMTOM_PID_MIC_5_UA);
	S(hph_current, TOMTOM_PID_MIC_5_UA);
	S(t_mic_pid, 100);
	S(t_ins_complete, 250);
	S(t_ins_retry, 200);
#undef S
#define S(X, Y) ((WCD9XXX_MBHC_CAL_PLUG_TYPE_PTR(codec_cal)->X) = (Y))
	S(v_no_mic, 30);

#ifdef CONFIG_MACH_LGE

#ifdef CONFIG_SND_SOC_ES9018
S(v_hs_max, 2800);
#else
S(v_hs_max, 2500);
#endif

#else
	S(v_hs_max, 2400);
#endif

#undef S
#define S(X, Y) ((WCD9XXX_MBHC_CAL_BTN_DET_PTR(codec_cal)->X) = (Y))
	S(c[0], 62);
	S(c[1], 124);
	S(nc, 1);
	S(n_meas, 3);
	S(mbhc_nsc, 11);
	S(n_btn_meas, 1);
	S(n_btn_con, 2);
	S(num_btn, WCD9XXX_MBHC_DEF_BUTTONS);
	S(v_btn_press_delta_sta, 100);
	S(v_btn_press_delta_cic, 50);
#undef S
	btn_cfg = WCD9XXX_MBHC_CAL_BTN_DET_PTR(codec_cal);
	btn_low = wcd9xxx_mbhc_cal_btn_det_mp(btn_cfg, MBHC_BTN_DET_V_BTN_LOW);
	btn_high = wcd9xxx_mbhc_cal_btn_det_mp(btn_cfg,
					       MBHC_BTN_DET_V_BTN_HIGH);
#if 0 // org
	btn_low[0] = -50;
	btn_high[0] = 90;
	btn_low[1] = 130;
	btn_high[1] = 220;
	btn_low[2] = 235;
	btn_high[2] = 335;
	btn_low[3] = 375;
	btn_high[3] = 655;
	btn_low[4] = 656;
	btn_high[4] = 660;
	btn_low[5] = 661;
	btn_high[5] = 670;
	btn_low[6] = 671;
	btn_high[6] = 680;
	btn_low[7] = 681;
	btn_high[7] = 690;
#else
#ifdef CONFIG_SND_SOC_HPH_WA
	btn_low[0] = 10;
	btn_high[0] = 110;
	btn_low[1] = 111;
	btn_high[1] = 190;
	btn_low[2] = 191;
	btn_high[2] = 359;
	btn_low[3] = 360;
	btn_high[3] = 750;
	btn_low[4] = -50;
	btn_high[4] = 9;
	btn_low[5] = 751;
	btn_high[5] = 754;
	btn_low[6] = 755;
	btn_high[6] = 756;
	btn_low[7] = 757;
	btn_high[7] = 758;
#else // sync with acdb
	btn_low[0] = -50;
	btn_high[0] = 110;
	btn_low[1] = 111;
	btn_high[1] = 190;
	btn_low[2] = 191;
	btn_high[2] = 359;
	btn_low[3] = 360;
	btn_high[3] = 750;
	btn_low[4] = 751;
	btn_high[4] = 752;
	btn_low[5] = 753;
	btn_high[5] = 754;
	btn_low[6] = 755;
	btn_high[6] = 756;
	btn_low[7] = 757;
	btn_high[7] = 758;
#endif
#endif
	n_ready = wcd9xxx_mbhc_cal_btn_det_mp(btn_cfg, MBHC_BTN_DET_N_READY);
	n_ready[0] = 80;
	n_ready[1] = 68;
	n_cic = wcd9xxx_mbhc_cal_btn_det_mp(btn_cfg, MBHC_BTN_DET_N_CIC);
	n_cic[0] = 60;
	n_cic[1] = 47;
	gain = wcd9xxx_mbhc_cal_btn_det_mp(btn_cfg, MBHC_BTN_DET_GAIN);
	gain[0] = 11;
	gain[1] = 9;

	return codec_cal;
}

static int msm_snd_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai_link *dai_link = rtd->dai_link;

	int ret = 0;
	u32 rx_ch[SLIM_MAX_RX_PORTS], tx_ch[SLIM_MAX_TX_PORTS];
	u32 rx_ch_cnt = 0, tx_ch_cnt = 0;
	u32 user_set_tx_ch = 0;
	u32 rx_ch_count;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		pr_debug("%s: rx_0_ch=%d\n", __func__, msm_slim_0_rx_ch);
		ret = snd_soc_dai_get_channel_map(codec_dai,
					&tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
		if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_5_RX) {
			pr_debug("%s: rx_5_ch=%d\n", __func__,
				  msm_slim_5_rx_ch);
			rx_ch_count = msm_slim_5_rx_ch;
		} else {
			pr_debug("%s: rx_0_ch=%d\n", __func__,
				  msm_slim_0_rx_ch);
			rx_ch_count = msm_slim_0_rx_ch;
		}
		ret = snd_soc_dai_set_channel_map(cpu_dai, 0, 0,
						  rx_ch_count, rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	} else {

		pr_debug("%s: %s_tx_dai_id_%d_ch=%d\n", __func__,
			 codec_dai->name, codec_dai->id, user_set_tx_ch);
		ret = snd_soc_dai_get_channel_map(codec_dai,
					 &tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map\n, err:%d\n",
				__func__, ret);
			goto end;
		}
		/* For <codec>_tx1 case */
		if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_0_TX)
			user_set_tx_ch = msm_slim_0_tx_ch;
		/* For <codec>_tx2 case */
		else if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_1_TX)
			user_set_tx_ch = params_channels(params);
		else if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_3_TX)
			/* DAI 5 is used for external EC reference from codec.
			 * Since Rx is fed as reference for EC, the config of
			 * this DAI is based on that of the Rx path.
			 */
			user_set_tx_ch = msm_slim_0_rx_ch;
		else if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_4_TX)
			user_set_tx_ch = msm_vi_feed_tx_ch;
		else
			user_set_tx_ch = tx_ch_cnt;

		pr_debug(
		"%s: msm_slim_0_tx_ch(%d) user_set_tx_ch(%d) tx_ch_cnt(%d) be_id %d\n",
			__func__, msm_slim_0_tx_ch, user_set_tx_ch,
			tx_ch_cnt, dai_link->be_id);

		ret = snd_soc_dai_set_channel_map(cpu_dai,
						  user_set_tx_ch, tx_ch, 0 , 0);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	}
end:
	return ret;
}

static struct snd_soc_ops msm8994_be_ops = {
	.hw_params = msm_snd_hw_params,
};

static int msm8994_slimbus_2_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret = 0;
	unsigned int rx_ch[SLIM_MAX_RX_PORTS], tx_ch[SLIM_MAX_TX_PORTS];
	unsigned int rx_ch_cnt = 0, tx_ch_cnt = 0;
	unsigned int num_tx_ch = 0;
	unsigned int num_rx_ch = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		num_rx_ch =  params_channels(params);
		pr_debug("%s: %s rx_dai_id = %d  num_ch = %d\n", __func__,
			codec_dai->name, codec_dai->id, num_rx_ch);
		ret = snd_soc_dai_get_channel_map(codec_dai,
				&tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
		ret = snd_soc_dai_set_channel_map(cpu_dai, 0, 0,
				num_rx_ch, rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	} else {
		num_tx_ch =  params_channels(params);
		pr_debug("%s: %s  tx_dai_id = %d  num_ch = %d\n", __func__,
			codec_dai->name, codec_dai->id, num_tx_ch);
		ret = snd_soc_dai_get_channel_map(codec_dai,
				&tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
		ret = snd_soc_dai_set_channel_map(cpu_dai,
				num_tx_ch, tx_ch, 0 , 0);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	}
end:
	return ret;
}

static struct snd_soc_ops msm8994_slimbus_2_be_ops = {
	.hw_params = msm8994_slimbus_2_hw_params,
};

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link msm8994_common_dai_links[] = {
	/* FrontEnd DAI Links */
	{
		.name = "MSM8994 Media1",
		.stream_name = "MultiMedia1",
		.cpu_dai_name	= "MultiMedia1",
		.platform_name  = "msm-pcm-dsp.0",
		.dynamic = 1,
		.async_ops = ASYNC_DPCM_SND_SOC_PREPARE,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA1
	},
	{
		.name = "MSM8994 Media2",
		.stream_name = "MultiMedia2",
		.cpu_dai_name   = "MultiMedia2",
		.platform_name  = "msm-pcm-dsp.0",
		.dynamic = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA2,
	},
	{
		.name = "Circuit-Switch Voice",
		.stream_name = "CS-Voice",
		.cpu_dai_name   = "CS-VOICE",
		.platform_name  = "msm-pcm-voice",
		.dynamic = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_CS_VOICE,
	},
	{
		.name = "MSM VoIP",
		.stream_name = "VoIP",
		.cpu_dai_name = "VoIP",
		.platform_name = "msm-voip-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_VOIP,
	},
	{
		.name = "MSM8994 ULL",
		.stream_name = "MultiMedia3",
		.cpu_dai_name = "MultiMedia3",
		.platform_name = "msm-pcm-dsp.2",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA3,
	},
	/* Hostless PCM purpose */
	{
		.name = "SLIMBUS_0 Hostless",
		.stream_name = "SLIMBUS_0 Hostless",
		.cpu_dai_name = "SLIMBUS0_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1, /* dai link has playback support */
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Primary MI2S TX_Hostless",
		.stream_name = "Primary MI2S_TX Hostless Capture",
		.cpu_dai_name = "PRI_MI2S_TX_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "MSM AFE-PCM RX",
		.stream_name = "AFE-PROXY RX",
		.cpu_dai_name = "msm-dai-q6-dev.241",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.platform_name = "msm-pcm-afe",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
	},
	{
		.name = "MSM AFE-PCM TX",
		.stream_name = "AFE-PROXY TX",
		.cpu_dai_name = "msm-dai-q6-dev.240",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.platform_name  = "msm-pcm-afe",
		.ignore_suspend = 1,
	},
	{
		.name = "MSM8994 Compress1",
		.stream_name = "Compress1",
		.cpu_dai_name = "MultiMedia4",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA4,
	},
	{
		.name = "AUXPCM Hostless",
		.stream_name = "AUXPCM Hostless",
		.cpu_dai_name = "AUXPCM_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "SLIMBUS_1 Hostless",
		.stream_name = "SLIMBUS_1 Hostless",
		.cpu_dai_name = "SLIMBUS1_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1, /* dai link has playback support */
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "SLIMBUS_3 Hostless",
		.stream_name = "SLIMBUS_3 Hostless",
		.cpu_dai_name = "SLIMBUS3_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1, /* dai link has playback support */
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "SLIMBUS_4 Hostless",
		.stream_name = "SLIMBUS_4 Hostless",
		.cpu_dai_name = "SLIMBUS4_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1, /* dai link has playback support */
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "VoLTE",
		.stream_name = "VoLTE",
		.cpu_dai_name = "VoLTE",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_VOLTE,
	},
	{
		.name = "MSM8994 LowLatency",
		.stream_name = "MultiMedia5",
		.cpu_dai_name = "MultiMedia5",
		.platform_name = "msm-pcm-dsp.1",
		.dynamic = 1,
		.async_ops = ASYNC_DPCM_SND_SOC_PREPARE,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
				SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA5,
	},
	{
		.name = "Listen 1 Audio Service",
		.stream_name = "Listen 1 Audio Service",
		.cpu_dai_name = "LSM1",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
			     SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM1,
	},
	/* Multiple Tunnel instances */
	{
		.name = "MSM8994 Compress2",
		.stream_name = "Compress2",
		.cpu_dai_name = "MultiMedia7",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA7,
	},
	{
		.name = "MSM8994 Compress3",
		.stream_name = "Compress3",
		.cpu_dai_name = "MultiMedia10",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA10,
	},
	{
		.name = "MSM8994 Compr8",
		.stream_name = "COMPR8",
		.cpu_dai_name = "MultiMedia8",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA8,
	},
	{
		.name = "QCHAT",
		.stream_name = "QCHAT",
		.cpu_dai_name = "QCHAT",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_QCHAT,
	},
	/* HDMI Hostless */
	{
		.name = "HDMI_RX_HOSTLESS",
		.stream_name = "HDMI_RX_HOSTLESS",
		.cpu_dai_name = "HDMI_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Voice2",
		.stream_name = "Voice2",
		.cpu_dai_name = "Voice2",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_VOICE2,
	},
	{
		.name = "INT_HFP_BT Hostless",
		.stream_name = "INT_HFP_BT Hostless",
		.cpu_dai_name = "INT_HFP_BT_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "MSM8994 HFP TX",
		.stream_name = "MultiMedia6",
		.cpu_dai_name = "MultiMedia6",
		.platform_name = "msm-pcm-loopback",
		.dynamic = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA6,
	},
	{
		.name = LPASS_BE_SLIMBUS_4_TX,
		.stream_name = "Slimbus4 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16393",
		.platform_name = "msm-pcm-hostless",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_vifeedback",
		.be_id = MSM_BACKEND_DAI_SLIMBUS_4_TX,
		.be_hw_params_fixup = msm_slim_4_tx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
	},
	/* Ultrasound RX DAI Link */
	{
		.name = "SLIMBUS_2 Hostless Playback",
		.stream_name = "SLIMBUS_2 Hostless Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16388",
		.platform_name = "msm-pcm-hostless",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_rx2",
		.ignore_suspend = 1,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ops = &msm8994_slimbus_2_be_ops,
	},
	/* Ultrasound TX DAI Link */
	{
		.name = "SLIMBUS_2 Hostless Capture",
		.stream_name = "SLIMBUS_2 Hostless Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16389",
		.platform_name = "msm-pcm-hostless",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_tx2",
		.ignore_suspend = 1,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ops = &msm8994_slimbus_2_be_ops,
	},
	/* LSM FE */
	{
		.name = "Listen 2 Audio Service",
		.stream_name = "Listen 2 Audio Service",
		.cpu_dai_name = "LSM2",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM2,
	},
	{
		.name = "Listen 3 Audio Service",
		.stream_name = "Listen 3 Audio Service",
		.cpu_dai_name = "LSM3",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM3,
	},
	{
		.name = "Listen 4 Audio Service",
		.stream_name = "Listen 4 Audio Service",
		.cpu_dai_name = "LSM4",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM4,
	},
	{
		.name = "Listen 5 Audio Service",
		.stream_name = "Listen 5 Audio Service",
		.cpu_dai_name = "LSM5",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM5,
	},
	{
		.name = "Listen 6 Audio Service",
		.stream_name = "Listen 6 Audio Service",
		.cpu_dai_name = "LSM6",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM6,
	},
	{
		.name = "Listen 7 Audio Service",
		.stream_name = "Listen 7 Audio Service",
		.cpu_dai_name = "LSM7",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM7,
	},
	{
		.name = "Listen 8 Audio Service",
		.stream_name = "Listen 8 Audio Service",
		.cpu_dai_name = "LSM8",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM8,
	},
	{
		.name = "MSM8994 Media9",
		.stream_name = "MultiMedia9",
		.cpu_dai_name = "MultiMedia9",
		.platform_name = "msm-pcm-dsp.0",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
				SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA9,
	},
	{
		.name = "VoWLAN",
		.stream_name = "VoWLAN",
		.cpu_dai_name = "VoWLAN",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_VOWLAN,
	},
	{
		.name = "MSM8994 Compress4",
		.stream_name = "Compress4",
		.cpu_dai_name = "MultiMedia11",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA11,
	},
	{
		.name = "MSM8994 Compress5",
		.stream_name = "Compress5",
		.cpu_dai_name = "MultiMedia12",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA12,
	},
	{
		.name = "MSM8994 Compress6",
		.stream_name = "Compress6",
		.cpu_dai_name = "MultiMedia13",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA13,
	},
	{
		.name = "MSM8994 Compress7",
		.stream_name = "Compress7",
		.cpu_dai_name = "MultiMedia14",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA14,
	},
	{
		.name = "MSM8994 Compress8",
		.stream_name = "Compress8",
		.cpu_dai_name = "MultiMedia15",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA15,
	},
	{
		.name = "MSM8994 Compress9",
		.stream_name = "Compress9",
		.cpu_dai_name = "MultiMedia16",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 /* this dainlink has playback support */
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA16,
	},
	/* CPE LSM direct dai-link */
	{
		.name = "CPE Listen service",
		.stream_name = "CPE Listen Audio Service",
		.cpu_dai_name = "CPE_LSM_NOHOST",
		.platform_name = "msm-cpe-lsm",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "tomtom_mad1",
		.codec_name = "tomtom_codec",
	},
	{
		.name = "MultiMedia3 Record",
		.stream_name = "MultiMedia3 Capture",
		.cpu_dai_name = "MultiMedia3",
		.platform_name = "msm-pcm-dsp.0",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA3,
	},
	/* End of FE DAI LINK */
	/* Backend FM DAI Links */
	{
		.name = LPASS_BE_INT_FM_RX,
		.stream_name = "Internal FM Playback",
		.cpu_dai_name = "msm-dai-q6-dev.12292",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_INT_FM_RX,
		.be_hw_params_fixup = msm_be_fm_hw_params_fixup,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_INT_FM_TX,
		.stream_name = "Internal FM Capture",
		.cpu_dai_name = "msm-dai-q6-dev.12293",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_INT_FM_TX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	/* Backend AFE DAI Links */
	{
		.name = LPASS_BE_AFE_PCM_RX,
		.stream_name = "AFE Playback",
		.cpu_dai_name = "msm-dai-q6-dev.224",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_AFE_PCM_RX,
		.be_hw_params_fixup = msm_proxy_rx_be_hw_params_fixup,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_AFE_PCM_TX,
		.stream_name = "AFE Capture",
		.cpu_dai_name = "msm-dai-q6-dev.225",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_AFE_PCM_TX,
		.be_hw_params_fixup = msm_proxy_tx_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	/* Primary AUX PCM Backend DAI Links */
	{
		.name = LPASS_BE_AUXPCM_RX,
		.stream_name = "AUX PCM Playback",
		.cpu_dai_name = "msm-dai-q6-auxpcm.1",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_AUXPCM_RX,
		.be_hw_params_fixup = msm_auxpcm_be_params_fixup,
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
	},
	{
		.name = LPASS_BE_AUXPCM_TX,
		.stream_name = "AUX PCM Capture",
		.cpu_dai_name = "msm-dai-q6-auxpcm.1",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_AUXPCM_TX,
		.be_hw_params_fixup = msm_auxpcm_be_params_fixup,
		.ignore_suspend = 1,
	},
	/* Secondary AUX PCM Backend DAI Links */
	{
		.name = LPASS_BE_SEC_AUXPCM_RX,
		.stream_name = "Sec AUX PCM Playback",
		.cpu_dai_name = "msm-dai-q6-auxpcm.2",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SEC_AUXPCM_RX,
		.be_hw_params_fixup = msm_auxpcm_be_params_fixup,
		.ops = &msm_sec_auxpcm_be_ops,
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		/* this dainlink has playback support */
	},
	{
		.name = LPASS_BE_SEC_AUXPCM_TX,
		.stream_name = "Sec AUX PCM Capture",
		.cpu_dai_name = "msm-dai-q6-auxpcm.2",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SEC_AUXPCM_TX,
		.be_hw_params_fixup = msm_auxpcm_be_params_fixup,
		.ops = &msm_sec_auxpcm_be_ops,
		.ignore_suspend = 1,
	},

	/* Backend DAI Links */
	{
		.name = LPASS_BE_SLIMBUS_0_RX,
		.stream_name = "Slimbus Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16384",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_rx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_0_RX,
		.init = &msm_audrx_init,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		.ignore_pmdown_time = 1, /* dai link has playback support */
		.ignore_suspend = 1,
		.ops = &msm8994_be_ops,
	},
	{
		.name = LPASS_BE_SLIMBUS_0_TX,
		.stream_name = "Slimbus Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16385",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_tx1",
		.no_pcm = 1,
		.async_ops = ASYNC_DPCM_SND_SOC_PREPARE,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_0_TX,
		.be_hw_params_fixup = msm_slim_0_tx_be_hw_params_fixup,
		.ignore_suspend = 1,
		.ops = &msm8994_be_ops,
	},
	{
		.name = LPASS_BE_SLIMBUS_1_RX,
		.stream_name = "Slimbus1 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16386",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_rx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_1_RX,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		/* dai link has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_1_TX,
		.stream_name = "Slimbus1 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16387",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_tx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_1_TX,
		.be_hw_params_fixup = msm_slim_0_tx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_3_RX,
		.stream_name = "Slimbus3 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16390",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_rx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_3_RX,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		/* dai link has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_3_TX,
		.stream_name = "Slimbus3 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16391",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_tx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_3_TX,
		.be_hw_params_fixup = msm_slim_0_tx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_4_RX,
		.stream_name = "Slimbus4 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16392",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name	= "tomtom_rx1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_4_RX,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		/* dai link has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	/* Incall Record Uplink BACK END DAI Link */
	{
		.name = LPASS_BE_INCALL_RECORD_TX,
		.stream_name = "Voice Uplink Capture",
		.cpu_dai_name = "msm-dai-q6-dev.32772",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_INCALL_RECORD_TX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	/* Incall Record Downlink BACK END DAI Link */
	{
		.name = LPASS_BE_INCALL_RECORD_RX,
		.stream_name = "Voice Downlink Capture",
		.cpu_dai_name = "msm-dai-q6-dev.32771",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_INCALL_RECORD_RX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	/* MAD BE */
	{
		.name = LPASS_BE_SLIMBUS_5_TX,
		.stream_name = "Slimbus5 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16395",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_mad1",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_5_TX,
		.be_hw_params_fixup = msm_slim_5_tx_be_hw_params_fixup,
		.ignore_suspend = 1,
		.ops = &msm8994_be_ops,
	},
	/* Incall Music BACK END DAI Link */
	{
		.name = LPASS_BE_VOICE_PLAYBACK_TX,
		.stream_name = "Voice Farend Playback",
		.cpu_dai_name = "msm-dai-q6-dev.32773",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_VOICE_PLAYBACK_TX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	/* Incall Music 2 BACK END DAI Link */
	{
		.name = LPASS_BE_VOICE2_PLAYBACK_TX,
		.stream_name = "Voice2 Farend Playback",
		.cpu_dai_name = "msm-dai-q6-dev.32770",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_VOICE2_PLAYBACK_TX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_PRI_MI2S_TX,
		.stream_name = "Primary MI2S Capture",
		.cpu_dai_name = "msm-dai-q6-mi2s.0",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_PRI_MI2S_TX,
		.be_hw_params_fixup = msm_tx_be_hw_params_fixup,
		.ops = &msm8994_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_5_RX,
		.stream_name = "Slimbus5 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16394",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tomtom_codec",
		.codec_dai_name = "tomtom_rx3",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_5_RX,
		.be_hw_params_fixup = msm_slim_5_rx_be_hw_params_fixup,
		.ops = &msm8994_be_ops,
		/* dai link has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
};

#ifndef CONFIG_SND_DISABLE_DUMMY_DAI
/* DAI LINK added by lge should be here */
/* even if functions is defeatured, dummy dai should be there */
/* if you need to create new dai link, u must add new one behind last one */
/* num of qct dia should be below LGE_DAI_LINK_ID_BASE */

#define LGE_DAI_LINK_ID_BASE	80

static struct snd_soc_dai_link msm8994_lge_dai_link[] = {
#if defined(CONFIG_SND_SOC_TAS2552)
	{
		.name = LPASS_BE_QUAT_MI2S_RX,
		.stream_name = "Quaternary MI2S Playback",
		.cpu_dai_name = "msm-dai-q6-mi2s.3",
		.platform_name = "msm-pcm-routing",
		.codec_name 	= "tas2552-codec.5-0040",
		.codec_dai_name = "tas2552-amplifier",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
		.be_hw_params_fixup = msm_be_quat_mi2s_hw_params_fixup,
		.ops = &msm8994_quat_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_QUAT_MI2S_TX,
		.stream_name = "Quaternary MI2S Capture",
		.cpu_dai_name = "msm-dai-q6-mi2s.3",
		.platform_name = "msm-pcm-hostless",
		.codec_name 	= "tas2552-codec.5-0040",
		.codec_dai_name = "tas2552-amplifier",
		.be_id = MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
		.be_hw_params_fixup = msm_be_quat_mi2s_hw_params_fixup,
		.ops = &msm8994_quat_mi2s_be_ops,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
	},
#else
	/* DUMMY DAI Link */
	{
		.name = "Dummy DAI 100",
		.stream_name = "MultiMedia1",
		.cpu_dai_name	= "MultiMedia1",
		.platform_name  = "msm-pcm-dsp.0",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA1
	},
	/* DUMMY DAI Link */
	{
		.name = "Dummy DAI 101",
		.stream_name = "MultiMedia1",
		.cpu_dai_name	= "MultiMedia1",
		.platform_name  = "msm-pcm-dsp.0",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA1
	},
#endif

#ifdef CONFIG_SND_LGE_DSDP_DUAL_AUDIO
	{
		.name = "Dual Audio",
		.stream_name = "Dual audio",
		.cpu_dai_name	= "MultiMedia2",
		.platform_name  = "msm-pcm-dsp.0",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA2
	},
#else
	/* DUMMY DAI Link */
	{
		.name = "Dummy DAI 102",
		.stream_name = "MultiMedia1",
		.cpu_dai_name	= "MultiMedia1",
		.platform_name  = "msm-pcm-dsp.0",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA1
	},
#endif
#ifdef CONFIG_SND_USE_QUAT_MI2S
    {
        .name = "QUAT_MI2S_RX Hostless",
        .stream_name = "QUAT_MI2S_RX Hostless",
        .cpu_dai_name = "QUAT_MI2S_RX_HOSTLESS",
        .platform_name = "msm-pcm-hostless",
        .dynamic = 1,
        .trigger = {SND_SOC_DPCM_TRIGGER_POST,
            SND_SOC_DPCM_TRIGGER_POST},
        .no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
        .ignore_suspend = 1,
        /* this dainlink has playback support */
        .ignore_pmdown_time = 1,
        .codec_dai_name = "snd-soc-dummy-dai",
        .codec_name = "snd-soc-dummy",
    },
#else
    /* DUMMY DAI Link */
    {
        .name = "Dummy DAI 103",
        .stream_name = "MultiMedia1",
        .cpu_dai_name	= "MultiMedia1",
        .platform_name  = "msm-pcm-dsp.0",
        .dynamic = 1,
        .trigger = {SND_SOC_DPCM_TRIGGER_POST,
            SND_SOC_DPCM_TRIGGER_POST},
        .codec_dai_name = "snd-soc-dummy-dai",
        .codec_name = "snd-soc-dummy",
        .ignore_suspend = 1,
        /* this dainlink has playback support */
        .ignore_pmdown_time = 1,
        .be_id = MSM_FRONTEND_DAI_MULTIMEDIA1
    },
#endif

#ifdef CONFIG_SND_USE_TERT_MI2S
	{
		.name = LPASS_BE_TERT_MI2S_RX,
		.stream_name = "Tertiary MI2S Playback",
		.cpu_dai_name = "msm-dai-q6-mi2s.2",
		.platform_name = "msm-pcm-routing",
		.codec_name = "es9018-codec.5-0048",
		.codec_dai_name = "es9018-hifi",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
		.be_hw_params_fixup = msm_be_tert_mi2s_hw_params_fixup,
		.ops = &msm8994_tert_mi2s_be_ops,
		.ignore_suspend = 1,
	},

#else
	/* DUMMY DAI Link */
	{
		.name = "Dummy DAI 104",
		.stream_name = "MultiMedia1",
		.cpu_dai_name	= "MultiMedia1",
		.platform_name  = "msm-pcm-dsp.0",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA1
	},
	/* DUMMY DAI Link */
#endif /* CONFIG_SND_USE_TERT_MI2S */
};

static struct snd_soc_dai_link msm8994_dummy_dai_link[] = {
	/* DUMMY DAI Link */
	{
		.name = "Dummy DAI",
		.stream_name = "MultiMedia1",
		.cpu_dai_name	= "MultiMedia1",
		.platform_name  = "msm-pcm-dsp.0",
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		/* this dainlink has playback support */
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA1
	},
};
#endif /*CONFIG_SND_DISABLE_DUMMY_DAI*/

static struct snd_soc_dai_link msm8994_hdmi_dai_link[] = {
/* HDMI BACK END DAI Link */
	{
		.name = LPASS_BE_HDMI,
		.stream_name = "HDMI Playback",
		.cpu_dai_name = "msm-dai-q6-hdmi.8",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-hdmi-audio-codec-rx",
		.codec_dai_name = "msm_hdmi_audio_codec_rx_dai",
		.no_pcm = 1,
		.be_id = MSM_BACKEND_DAI_HDMI_RX,
		.be_hw_params_fixup = msm8994_hdmi_be_hw_params_fixup,
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
};

static struct snd_soc_dai_link msm8994_dai_links[
#ifndef CONFIG_SND_DISABLE_DUMMY_DAI
					 LGE_DAI_LINK_ID_BASE +
					 ARRAY_SIZE(msm8994_lge_dai_link)];
#else
					 ARRAY_SIZE(msm8994_common_dai_links) +
					 ARRAY_SIZE(msm8994_hdmi_dai_link)];
#endif /*CONFIG_SND_DISABLE_DUMMY_DAI*/


struct snd_soc_card snd_soc_card_msm8994 = {
	.name		= "msm8994-tomtom-snd-card",
};

static int msm8994_populate_dai_link_component_of_node(
					struct snd_soc_card *card)
{
	int i, index, ret = 0;
	struct device *cdev = card->dev;
	struct snd_soc_dai_link *dai_link = card->dai_link;
	struct device_node *np;

	if (!cdev) {
		pr_err("%s: Sound card device memory NULL\n", __func__);
		return -ENODEV;
	}

	for (i = 0; i < card->num_links; i++) {
		if (dai_link[i].platform_of_node && dai_link[i].cpu_of_node)
			continue;

		/* populate platform_of_node for snd card dai links */
		if (dai_link[i].platform_name &&
		    !dai_link[i].platform_of_node) {
			index = of_property_match_string(cdev->of_node,
						"asoc-platform-names",
						dai_link[i].platform_name);
			if (index < 0) {
				pr_debug("%s: No match found for platform name: %s\n",
					__func__, dai_link[i].platform_name);
				ret = index;
				goto err;
			}
			np = of_parse_phandle(cdev->of_node, "asoc-platform",
					      index);
			if (!np) {
				pr_err("%s: retrieving phandle for platform %s, index %d failed\n",
					__func__, dai_link[i].platform_name,
					index);
				ret = -ENODEV;
				goto err;
			}
			dai_link[i].platform_of_node = np;
			dai_link[i].platform_name = NULL;
		}

		/* populate cpu_of_node for snd card dai links */
		if (dai_link[i].cpu_dai_name && !dai_link[i].cpu_of_node) {
			index = of_property_match_string(cdev->of_node,
						 "asoc-cpu-names",
						 dai_link[i].cpu_dai_name);
			if (index >= 0) {
				np = of_parse_phandle(cdev->of_node, "asoc-cpu",
						index);
				if (!np) {
					pr_err("%s: retrieving phandle for cpu dai %s failed\n",
						__func__,
						dai_link[i].cpu_dai_name);
					ret = -ENODEV;
					goto err;
				}
				dai_link[i].cpu_of_node = np;
				dai_link[i].cpu_dai_name = NULL;
			}
		}

		/* populate codec_of_node for snd card dai links */
		if (dai_link[i].codec_name && !dai_link[i].codec_of_node) {
			index = of_property_match_string(cdev->of_node,
						 "asoc-codec-names",
						 dai_link[i].codec_name);
			if (index < 0)
				continue;
			np = of_parse_phandle(cdev->of_node, "asoc-codec",
					      index);
			if (!np) {
				pr_err("%s: retrieving phandle for codec %s failed\n",
					__func__, dai_link[i].codec_name);
				ret = -ENODEV;
				goto err;
			}
			dai_link[i].codec_of_node = np;
			dai_link[i].codec_name = NULL;
		}
	}

err:
	return ret;
}

static int msm8994_prepare_codec_mclk(struct snd_soc_card *card)
{
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	int ret;
	if (pdata->mclk_gpio) {
		ret = gpio_request(pdata->mclk_gpio, "TOMTOM_CODEC_PMIC_MCLK");
		if (ret) {
			dev_err(card->dev,
				"%s: request mclk gpio failed %d, err:%d\n",
				__func__, pdata->mclk_gpio, ret);
			return ret;
		}
	}

	return 0;
}

static int msm8994_prepare_us_euro(struct snd_soc_card *card)
{
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	int ret;
	if (pdata->us_euro_gpio >= 0) {
		dev_dbg(card->dev, "%s: us_euro gpio request %d", __func__,
			pdata->us_euro_gpio);
		ret = gpio_request(pdata->us_euro_gpio, "TOMTOM_CODEC_US_EURO");
		if (ret) {
			dev_err(card->dev,
				"%s: Failed to request codec US/EURO gpio %d error %d\n",
				__func__, pdata->us_euro_gpio, ret);
			return ret;
		}
	}

	return 0;
}

static int msm8994_asoc_machine_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_card_msm8994;
	struct msm8994_asoc_mach_data *pdata;
	const char *mbhc_audio_jack_type = NULL;
#ifdef CONFIG_SND_EAR_MIC_SWITCH
        int plug_gpio = 0;
#endif
	int ret;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "No platform supplied from device tree\n");
		return -EINVAL;
	}

	pdata = devm_kzalloc(&pdev->dev,
			sizeof(struct msm8994_asoc_mach_data), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "Can't allocate msm8994_asoc_mach_data\n");
		return -ENOMEM;
	}

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, pdata);

	ret = snd_soc_of_parse_card_name(card, "qcom,model");
	if (ret) {
		dev_err(&pdev->dev, "parse card name failed, err:%d\n",
			ret);
		goto err;
	}

	ret = snd_soc_of_parse_audio_routing(card,
			"qcom,audio-routing");
	if (ret) {
		dev_err(&pdev->dev, "parse audio routing failed, err:%d\n",
			ret);
		goto err;
	}
	ret = of_property_read_u32(pdev->dev.of_node,
			"qcom,tomtom-mclk-clk-freq", &pdata->mclk_freq);
	if (ret) {
		dev_err(&pdev->dev,
			"Looking up %s property in node %s failed, err%d\n",
			"qcom,tomtom-mclk-clk-freq",
			pdev->dev.of_node->full_name, ret);
		goto err;
	}

	if (pdata->mclk_freq != 9600000) {
		dev_err(&pdev->dev, "unsupported mclk freq %u\n",
			pdata->mclk_freq);
		ret = -EINVAL;
		goto err;
	}

	pdata->mclk_gpio = of_get_named_gpio(pdev->dev.of_node,
				"qcom,cdc-mclk-gpios", 0);
	if (pdata->mclk_gpio < 0) {
		dev_err(&pdev->dev,
			"Looking up %s property in node %s failed %d\n",
			"qcom, cdc-mclk-gpios", pdev->dev.of_node->full_name,
			pdata->mclk_gpio);
		ret = -ENODEV;
		goto err;
	}

	ret = msm8994_prepare_codec_mclk(card);
	if (ret) {
		dev_err(&pdev->dev, "prepare_codec_mclk failed, err:%d\n",
			ret);
		goto err;
	}

	mbhc_cfg.mclk_rate = pdata->mclk_freq;
	if (of_property_read_bool(pdev->dev.of_node, "qcom,hdmi-audio-rx")&&!of_property_read_bool(pdev->dev.of_node, "qcom,hdmi-audio-rx,no-use")){
		dev_info(&pdev->dev, "%s: hdmi audio support present\n",
				__func__);

		memcpy(msm8994_dai_links, msm8994_common_dai_links,
			sizeof(msm8994_common_dai_links));

		memcpy(msm8994_dai_links + ARRAY_SIZE(msm8994_common_dai_links),
			msm8994_hdmi_dai_link, sizeof(msm8994_hdmi_dai_link));

		card->dai_link	= msm8994_dai_links;
#ifndef CONFIG_SND_DISABLE_DUMMY_DAI
		card->num_links = ARRAY_SIZE(msm8994_common_dai_links)
							+ ARRAY_SIZE(msm8994_hdmi_dai_link);
#else
		card->num_links = ARRAY_SIZE(msm8994_dai_links);
#endif /*CONFIG_SND_DISABLE_DUMMY_DAI*/

	} else {
  	    dev_info(&pdev->dev, "%s: No hdmi audio support\n", __func__);
#ifndef CONFIG_SND_DISABLE_DUMMY_DAI
		memcpy(msm8994_dai_links, msm8994_common_dai_links,
			sizeof(msm8994_common_dai_links));
		card->dai_link	= msm8994_dai_links;
#else
		card->dai_link	= msm8994_common_dai_links;
#endif /*CONFIG_SND_DISABLE_DUMMY_DAI*/
	card->num_links = ARRAY_SIZE(msm8994_common_dai_links);
	}

#ifndef CONFIG_SND_DISABLE_DUMMY_DAI
	/* Filled dummy dai behind LGE_DAI_LINK_ID_BASE */
	{
		static char dummy_dai_name[LGE_DAI_LINK_ID_BASE][50];
		int i;
		for(i = card->num_links; i < LGE_DAI_LINK_ID_BASE; i++) {
			struct snd_soc_dai_link *link = msm8994_dai_links + i;
			memcpy(link, msm8994_dummy_dai_link,
				sizeof(msm8994_dummy_dai_link));
			sprintf(&dummy_dai_name[i][0], "Dummy DAI %d", i);
			link->name = &dummy_dai_name[i][0];
		}
		card->num_links = LGE_DAI_LINK_ID_BASE;
	}
#endif

#ifndef CONFIG_SND_DISABLE_DUMMY_DAI
#if defined(CONFIG_SND_SOC_TAS2552)
	if (lge_get_board_revno() >= HW_REV_B) {
		if (!strcmp(msm8994_lge_dai_link[0].codec_name, "tas2552-codec.5-0040"))
			msm8994_lge_dai_link[0].codec_name = "tas2552-codec.5-0041";
		if (!strcmp(msm8994_lge_dai_link[1].codec_name, "tas2552-codec.5-0040"))
			msm8994_lge_dai_link[1].codec_name = "tas2552-codec.5-0041";
	}
#if defined(CONFIG_MACH_MSM8992_PPLUS)
	msm8994_lge_dai_link[0].codec_name = "tas2552-codec.5-0041";
	msm8994_lge_dai_link[1].codec_name = "tas2552-codec.5-0041";

	if (lge_get_board_revno() < HW_REV_0) {
		if (!strcmp(msm8994_lge_dai_link[4].codec_name, "es9018-codec.5-0048"))
		{
			/* DUMMY DAI Link */
			msm8994_lge_dai_link[4].name = "Dummy DAI 104";
			msm8994_lge_dai_link[4].stream_name = "MultiMedia1";
			msm8994_lge_dai_link[4].cpu_dai_name	= "MultiMedia1";
			msm8994_lge_dai_link[4].platform_name  = "msm-pcm-dsp.0";
			msm8994_lge_dai_link[4].dynamic = 1;
			msm8994_lge_dai_link[4].trigger[0] = SND_SOC_DPCM_TRIGGER_POST;
			msm8994_lge_dai_link[4].trigger[1] = SND_SOC_DPCM_TRIGGER_POST;
			msm8994_lge_dai_link[4].codec_dai_name = "snd-soc-dummy-dai";
			msm8994_lge_dai_link[4].codec_name = "snd-soc-dummy";
			msm8994_lge_dai_link[4].ignore_suspend = 1;
			/* this dainlink has playback support */
			msm8994_lge_dai_link[4].ignore_pmdown_time = 1;
			msm8994_lge_dai_link[4].be_id = MSM_FRONTEND_DAI_MULTIMEDIA1;
			msm8994_lge_dai_link[4].no_pcm = 0;
			msm8994_lge_dai_link[4].be_hw_params_fixup = NULL;
			msm8994_lge_dai_link[4].ops = NULL;
			/* DUMMY DAI Link */
		}
	}
#endif
#endif

	/* Append DAIs added by LGE in DAI LINK*/
	memcpy(msm8994_dai_links + card->num_links,
		msm8994_lge_dai_link, sizeof(msm8994_lge_dai_link));
	card->dai_link	= msm8994_dai_links;
	card->num_links += ARRAY_SIZE(msm8994_lge_dai_link);
#endif /*CONFIG_SND_INCALL_MUSIC_SUPPORT*/

	mutex_init(&cdc_mclk_mutex);
	atomic_set(&prim_auxpcm_rsc_ref, 0);
	atomic_set(&sec_auxpcm_rsc_ref, 0);
#ifdef CONFIG_SND_USE_TERT_MI2S
	atomic_set(&tert_mi2s_rsc_ref, 0);
#endif
#ifdef CONFIG_SND_USE_QUAT_MI2S
	atomic_set(&quat_mi2s_rsc_ref, 0);
#endif
	spdev = pdev;

#ifdef CONFIG_SND_SOC_HPH_WA
	ret = of_get_named_gpio(spdev->dev.of_node,
					   "dac,mbhc-sens-gpio", 0);
	if (ret >= 0) {
		mbhc_cfg.mbhc_det_gpio= ret;
		gpio_request(mbhc_cfg.mbhc_det_gpio, "mbhc_detect_disconnect");
		gpio_direction_output(mbhc_cfg.mbhc_det_gpio, 1);
		printk("%s : mbhc_cfg.mbhc_det_gpio - %d\n", __func__, mbhc_cfg.mbhc_det_gpio);
	}
	else{
		pr_err("%s:  setting failed mbhc detect disconnect ",__func__);
	}
#endif
#ifdef CONFIG_SND_EAR_MIC_SWITCH
        plug_gpio = of_get_named_gpio(spdev->dev.of_node,
					   "dac,hph-aud-sw", 0);
	if (plug_gpio >= 0) {
		mbhc_cfg.hph_aud_sw = plug_gpio;
		ret = gpio_request(mbhc_cfg.hph_aud_sw, "hph_aud_sw switch");
		if (ret) {
			pr_err("%s: hph_aud_sw request failed, ret:%d\n",
				__func__, ret);
		}
		gpio_direction_output(mbhc_cfg.hph_aud_sw, 0);
	}
#endif

	ret = msm8994_populate_dai_link_component_of_node(card);
	if (ret) {
		ret = -EPROBE_DEFER;
		goto err;
	}

	mbhc_cfg.gpio_level_insert = of_property_read_bool(pdev->dev.of_node,
					"qcom,headset-jack-type-NC");

	ret = snd_soc_register_card(card);
	if (ret == -EPROBE_DEFER) {
		goto err;
	} else if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto err;
	}
	ret = of_property_read_string(pdev->dev.of_node,
		"qcom,mbhc-audio-jack-type", &mbhc_audio_jack_type);
	if (ret) {
		dev_dbg(&pdev->dev, "Looking up %s property in node %s failed",
			"qcom,mbhc-audio-jack-type",
			pdev->dev.of_node->full_name);
		mbhc_cfg.hw_jack_type = FOUR_POLE_JACK;
		mbhc_cfg.enable_anc_mic_detect = false;
		dev_dbg(&pdev->dev, "Jack type properties set to default");
	} else {
		if (!strcmp(mbhc_audio_jack_type, "4-pole-jack")) {
			mbhc_cfg.hw_jack_type = FOUR_POLE_JACK;
			mbhc_cfg.enable_anc_mic_detect = false;
			dev_dbg(&pdev->dev, "This hardware has 4 pole jack");
		} else if (!strcmp(mbhc_audio_jack_type, "5-pole-jack")) {
			mbhc_cfg.hw_jack_type = FIVE_POLE_JACK;
			mbhc_cfg.enable_anc_mic_detect = true;
			dev_dbg(&pdev->dev, "This hardware has 5 pole jack");
		} else if (!strcmp(mbhc_audio_jack_type, "6-pole-jack")) {
			mbhc_cfg.hw_jack_type = SIX_POLE_JACK;
			mbhc_cfg.enable_anc_mic_detect = true;
			dev_dbg(&pdev->dev, "This hardware has 6 pole jack");
		} else {
			mbhc_cfg.hw_jack_type = FOUR_POLE_JACK;
			mbhc_cfg.enable_anc_mic_detect = false;
			dev_dbg(&pdev->dev, "Unknown value, set to default");
		}
	}

	ret = of_property_read_u32(pdev->dev.of_node,
				"qcom,mbhc-micbias-enable-flags",
				(u32*)&mbhc_cfg.micbias_enable_flags);
	if (ret)
		dev_err(&pdev->dev, "failed to get %s\n",
					"qcom,mbhc-micbias-enable-flags");

	/* Parse US-Euro gpio info from DT. Report no error if us-euro
	 * entry is not found in DT file as some targets do not support
	 * US-Euro detection
	 */
	pdata->us_euro_gpio = of_get_named_gpio(pdev->dev.of_node,
				"qcom,us-euro-gpios", 0);
	if (pdata->us_euro_gpio < 0) {
		dev_info(&pdev->dev, "property %s not detected in node %s",
			"qcom,us-euro-gpios",
			pdev->dev.of_node->full_name);
	} else {
		dev_dbg(&pdev->dev, "%s detected %d",
			"qcom,us-euro-gpios", pdata->us_euro_gpio);
		mbhc_cfg.swap_gnd_mic = msm8994_swap_gnd_mic;
	}

	ret = msm8994_prepare_us_euro(card);
	if (ret)
		dev_info(&pdev->dev, "msm8994_prepare_us_euro failed (%d)\n",
			ret);

	/* Parse pinctrl info from devicetree */
	ret = msm_get_pinctrl(pdev);
	if (!ret) {
		pr_debug("%s: pinctrl parsing successful\n", __func__);
	} else {
		dev_info(&pdev->dev,
			"%s: Parsing pinctrl failed with %d. Cannot use Ports\n",
			__func__, ret);
		goto err;
	}
	ret = apq8094_db_device_init();
	if (ret) {
		pr_err("%s: DB8094 init ext devices stat IRQ failed (%d)\n",
				__func__, ret);
		goto err1;
	}
	dev_info(&pdev->dev, "%s: done.\n", __func__);

	return 0;

err1:
	msm_release_pinctrl(pdev);
err:
	if (pdata->mclk_gpio > 0) {
		dev_dbg(&pdev->dev, "%s free gpio %d\n",
			__func__, pdata->mclk_gpio);
		gpio_free(pdata->mclk_gpio);
		pdata->mclk_gpio = 0;
	}
	if (pdata->us_euro_gpio > 0) {
		dev_dbg(&pdev->dev, "%s free us_euro gpio %d\n",
			__func__, pdata->us_euro_gpio);
		gpio_free(pdata->us_euro_gpio);
		pdata->us_euro_gpio = 0;
	}
	devm_kfree(&pdev->dev, pdata);
	return ret;
}

static int msm8994_asoc_machine_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8994_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);

	if (gpio_is_valid(ext_us_amp_gpio))
		gpio_free(ext_us_amp_gpio);

	gpio_free(pdata->mclk_gpio);
	gpio_free(pdata->us_euro_gpio);

	msm8994_audio_plug_device_remove(msm8994_liquid_dock_dev);

	msm8994_audio_plug_device_remove(apq8094_db_ext_bp_out_dev);
	msm8994_audio_plug_device_remove(apq8094_db_ext_fp_in_dev);
	msm8994_audio_plug_device_remove(apq8094_db_ext_fp_out_dev);

	msm_release_pinctrl(pdev);
	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id msm8994_asoc_machine_of_match[]  = {
	{ .compatible = "qcom,msm8994-asoc-snd", },
	{},
};

static struct platform_driver msm8994_asoc_machine_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = msm8994_asoc_machine_of_match,
	},
	.probe = msm8994_asoc_machine_probe,
	.remove = msm8994_asoc_machine_remove,
};
module_platform_driver(msm8994_asoc_machine_driver);

MODULE_DESCRIPTION("ALSA SoC msm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, msm8994_asoc_machine_of_match);
