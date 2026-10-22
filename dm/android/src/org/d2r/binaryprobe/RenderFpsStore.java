package org.d2r.binaryprobe;

import android.content.Context;
import android.util.AtomicFile;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * 出图帧率档位的存档（60 / 40 / 30）。
 *
 * <p>存成 "60" / "40" / "30" 三个纯 ASCII 数字，写在 d2r-mobile/diagnostics/present-hz.txt；
 * 那个路径同时是诊断用的 marker（运行时改它就能立即生效），所以玩家设置与诊断开关共用一处，
 * 不用两套代码。
 *
 * <p>为什么不做成「省电模式」「40 帧」两个独立开关：这两个值本来就互斥，单选组（RadioGroup）
 * 表达得更直接，也少一次「两个都勾算哪个」的疑问。
 *
 * <p>Present frame-rate tier storage (60 / 40 / 30). Written as a plain ASCII number into
 * d2r-mobile/diagnostics/present-hz.txt — the same path the diagnostics marker uses, so the player
 * setting and the diagnostic switch share one mechanism instead of two code paths. A single radio
 * group is used rather than two independent switches because the tiers are mutually exclusive.
 */
final class RenderFpsStore {
    /** 三档：60（性能优先）、40（平衡）、30（最省电）。 */
    static final int[] TIERS = {60, 40, 30};

    private final AtomicFile file;

    RenderFpsStore(Context context) {
        file = new AtomicFile(new File(context.getFilesDir(), "d2r-mobile/diagnostics/present-hz.txt"));
    }

    /** 读不到或内容不认识时按 60 帧（缺省档）算。 */
    int read() throws IOException {
        if (!file.getBaseFile().exists() && !new File(file.getBaseFile() + ".bak").exists()) return 60;
        try (FileInputStream in = file.openRead()) {
            ByteArrayOutputStream data = new ByteArrayOutputStream();
            for (int b; (b = in.read()) != -1; ) {
                if (data.size() == 15) throw new IOException("帧率设置过长");
                data.write(b);
            }
            String text = new String(data.toByteArray(), StandardCharsets.US_ASCII).trim();
            try {
                int hz = Integer.parseInt(text);
                for (int tier : TIERS) if (tier == hz) return hz;
                throw new NumberFormatException(text);
            } catch (NumberFormatException e) {
                throw new IOException("帧率设置无效：" + text, e);
            }
        }
    }

    void write(int hz) throws IOException {
        boolean known = false;
        for (int tier : TIERS) if (tier == hz) known = true;
        if (!known) throw new IOException("帧率设置无效：" + hz);
        byte[] data = Integer.toString(hz).getBytes(StandardCharsets.US_ASCII);
        FileOutputStream out = null;
        try {
            out = file.startWrite();
            out.write(data);
            out.getFD().sync();
            file.finishWrite(out);
            out = null;
            // AtomicFile 会把部分提交失败只记进日志而不抛出；读回来核对，避免下次启动用了没落盘的值。
            // AtomicFile logs some commit failures instead of throwing; read back so the next start
            // never uses a value that did not reach disk.
            if (read() != hz) throw new IOException("帧率设置提交失败");
        } catch (IOException e) {
            if (out != null) file.failWrite(out);
            throw e;
        }
    }
}
