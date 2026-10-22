package org.d2r.binaryprobe;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.res.ColorStateList;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import java.io.IOException;

/** Native system-menu surface; the platform owns focus, Back and switch semantics. */
final class RendererSettingsDialog {
    interface ScaleRequest { int apply(float scale); }
    /* 面板配色沿用触屏控件那一套暗金：INK 半透明底、RIM 描边、TEXT 米白、PRESSED 高亮琥珀。
     * 新增两级：CARD 是每节的卡片底（比整体底稍亮一层，靠描边分层）、GOLD 用于节标题与分隔线。
     * The panel reuses the touch controls' dark-gold palette: INK translucent fill, RIM outline, TEXT
     * off-white, PRESSED amber highlight. Two levels are added: CARD for each section's card
     * background (one step lighter than the panel so the outline carries the layering) and GOLD for
     * section headings and dividers. */
    private static final int INK=TouchControlsView.INK,RIM=TouchControlsView.RIM,TEXT=TouchControlsView.TEXT,PRESSED=TouchControlsView.PRESSED;
    private static final int CARD=0x66201511,CARD_RIM=0x99a08359,GOLD=0xffd8b173,MUTED=0xff9c8a70;
    private final Activity activity;
    private final RendererOptionsStore store;
    private final RenderQualityStore qualityStore;
    private final ScaleRequest scaleRequest;
    private final RadioButton automatic,original,balanced,performance;
    private final TextView qualityStatus;
    /* 帧率档位（60 / 40 / 30）：独立的一组单选 + 自己的存储与状态行 */
    private final RenderFpsStore fpsStore;
    private final RadioButton fps60,fps40,fps30;
    private final TextView fpsStatus;
    private boolean fpsReadFailed;
    private String fpsError;
    private int fpsDesired;
    private final Switch clear,writeback;
    private final TextView status;
    private final AlertDialog dialog;
    private RendererOptions desired;
    private boolean binding,readFailed;
    private String error;
    private int applied;
    private float appliedScale,desiredScale;
    private boolean qualityReadFailed;
    private String qualityError;
    private RendererSettingsDialog(Activity activity,int applied,float appliedScale,ScaleRequest scaleRequest,View trigger,Runnable closed){
        this.activity=activity;this.applied=applied;store=new RendererOptionsStore(activity);
        this.appliedScale=appliedScale;this.scaleRequest=scaleRequest;qualityStore=new RenderQualityStore(activity);
        this.fpsStore=new RenderFpsStore(activity);
        LinearLayout body=new LinearLayout(activity);body.setOrientation(LinearLayout.VERTICAL);body.setPadding(dp(16),dp(4),dp(16),dp(10));
        /* 画质模式：卡片内是单选项 + 说明 + 状态 */
        section(body,"画质模式");
        LinearLayout qualityCard=card(body);
        RadioGroup quality=new RadioGroup(activity);quality.setOrientation(LinearLayout.VERTICAL);qualityCard.addView(quality);
        automatic=qualityOption(quality,RenderQuality.description(RenderQuality.AUTO));
        original=qualityOption(quality,RenderQuality.description(RenderQuality.ORIGINAL));
        balanced=qualityOption(quality,RenderQuality.description(RenderQuality.BALANCED));
        performance=qualityOption(quality,RenderQuality.description(RenderQuality.PERFORMANCE));
        hint(qualityCard,"自动模式在掉帧时动态降低 3D 分辨率；原画质固定 100%；均衡固定 90%；性能固定 75%。界面和文字保持当前输出分辨率。");
        qualityStatus=statusLine(qualityCard,"正在读取画质设置…");
        /* 帧率档位：单选，立即生效（节拍器每帧读一次设置，不用重进游戏）。
         * 这是省电的主要手段——帧率降下来，CPU 与 GPU 的忙碌时间同比减少，机身温度和功耗都降。
         * 实测（骁龙 8G3 平板，2000×3200，进局后）：60 档 CPU 274% / 温度 84.5°C，
         * 40 档 236% / 70.9°C，30 档 217% / 62.6°C；40 与 30 两档帧节奏完全稳定。
         * 120 Hz 屏上节拍器只取整数分之一（60 / 40 / 30），所以只给这三档。
         * Frame-rate tier: single choice, takes effect immediately (the pacer reads the setting every
         * frame, no restart). This is the main power lever — a lower cap cuts CPU and GPU busy time
         * proportionally, lowering both temperature and draw. Measured on a Snapdragon 8G3 tablet at
         * 2000x3200 in game: the 60 tier runs CPU 274% / 84.5C, 40 runs 236% / 70.9C, 30 runs
         * 217% / 62.6C, and the 40 and 30 tiers keep a perfectly steady cadence. A 120 Hz panel only
         * takes integer fractions (60 / 40 / 30), so only those three tiers are offered. */
        section(body,"帧率");
        LinearLayout fpsCard=card(body);
        RadioGroup fpsGroup=new RadioGroup(activity);fpsGroup.setOrientation(LinearLayout.VERTICAL);fpsCard.addView(fpsGroup);
        fps60=qualityOption(fpsGroup,"60 帧（性能优先，最流畅）");
        fps40=qualityOption(fpsGroup,"40 帧（平衡：省电且帧节奏稳定）");
        fps30=qualityOption(fpsGroup,"30 帧（最省电，温度最低）");
        hint(fpsCard,"立即生效，不需要重新进入游戏。降低帧率能明显降低机身温度与耗电。");
        fpsStatus=statusLine(fpsCard,"正在读取帧率设置…");
        section(body,"图形优化");
        LinearLayout optCard=card(body);
        clear=option(optCard,"清屏合并","将符合条件的整屏清除并入绘制，减少独立清除和显存读取。");
        divider(optCard);
        writeback=option(optCard,"省略无用写回","确认内容在读取前会被完整覆盖时，省掉这次显存保存。");
        hint(optCard,"运行中生效并自动保存，可在同一场景往返对比。收益随场景变化，不保证提高帧率；若画面异常，可关闭对应项。");
        status=statusLine(optCard,"正在读取设置…");
        ScrollView scroll=new ScrollView(activity);scroll.setFillViewport(false);scroll.addView(body);
        dialog=new AlertDialog.Builder(activity).setView(scroll).setPositiveButton("返回游戏",null).create();
        dialog.setOnDismissListener(d->{closed.run();if(!activity.isFinishing())trigger.requestFocus();});
        clear.setOnCheckedChangeListener((v,on)->change(RendererOptions.CLEAR_MERGE,on));
        writeback.setOnCheckedChangeListener((v,on)->change(RendererOptions.STORE_ELIDE,on));
        automatic.setOnClickListener(v->changeQuality(RenderQuality.AUTO));
        original.setOnClickListener(v->changeQuality(RenderQuality.ORIGINAL));
        balanced.setOnClickListener(v->changeQuality(RenderQuality.BALANCED));
        performance.setOnClickListener(v->changeQuality(RenderQuality.PERFORMANCE));
        fps60.setOnClickListener(v->changeFps(60));
        fps40.setOnClickListener(v->changeFps(40));
        fps30.setOnClickListener(v->changeFps(30));
        try{desiredScale=qualityStore.read();bindQuality();}
        catch(IOException e){qualityReadFailed=true;qualityError="画质设置读取失败，请返回游戏后重新打开菜单。";automatic.setEnabled(false);original.setEnabled(false);balanced.setEnabled(false);performance.setEnabled(false);qualityFeedback();}
        try{fpsDesired=fpsStore.read();bindFps();}
        catch(IOException e){fpsReadFailed=true;fpsError="帧率设置读取失败，请返回游戏后重新打开菜单。";fps60.setEnabled(false);fps40.setEnabled(false);fps30.setEnabled(false);fpsFeedback();}
        reload();
    }
    static RendererSettingsDialog show(Activity activity,int applied,float scale,ScaleRequest request,View trigger,Runnable closed){
        RendererSettingsDialog panel=new RendererSettingsDialog(activity,applied,scale,request,trigger,closed);GameActivity.installDialogBack(panel.dialog);panel.dialog.show();
        /* 面板底：整体半透明墨色 + 金色描边，和游戏内的暗金界面同一族；
         * 标题用金色并加大字距（AlertDialog 没设 setTitle，改用自定义标题视图，才能控制颜色与间距）。
         * Panel: translucent ink with a gold outline to match the game's dark-gold UI. The title is
         * gold and letter-spaced — setTitle is not used because a custom title view is needed to
         * control its colour and spacing. */
        panel.dialog.getWindow().setBackgroundDrawable(panel.shape(0xf21a120c,GOLD,14));
        panel.dialog.getWindow().setLayout(Math.min(panel.dp(600),activity.getResources().getDisplayMetrics().widthPixels-panel.dp(36)),WindowManager.LayoutParams.WRAP_CONTENT);
        TextView title=new TextView(activity);title.setText("系统菜单");title.setTextColor(GOLD);title.setTextSize(20);
        title.setLetterSpacing(0.10f);title.setGravity(Gravity.CENTER_VERTICAL);
        title.setPadding(panel.dp(24),panel.dp(18),panel.dp(24),panel.dp(6));
        panel.dialog.setCustomTitle(title);
        panel.dialog.getButton(AlertDialog.BUTTON_POSITIVE).setTextColor(GOLD);
        panel.automatic.requestFocus();return panel;
    }
    private int dp(int n){return Math.round(n*activity.getResources().getDisplayMetrics().density);}
    /* ---------- 外观构件 ---------- */
    /** 圆角描边底：暗金风格里所有卡片、选项行、按钮共用这一种形状。 */
    private GradientDrawable shape(int fill,int stroke,int radius){
        GradientDrawable d=new GradientDrawable();d.setShape(GradientDrawable.RECTANGLE);
        d.setCornerRadius(dp(radius));d.setColor(fill);d.setStroke(Math.max(1,dp(1)),stroke);return d;
    }
    /** 选项行的底：未选中 = 卡片底 + 淡描边；选中 = 琥珀色薄底 + 金色描边。让当前项一眼可见。 */
    private StateListDrawable optionBackground(){
        StateListDrawable s=new StateListDrawable();
        s.addState(new int[]{android.R.attr.state_checked},shape(0x33d8b173,GOLD,8));
        s.addState(new int[]{android.R.attr.state_pressed},shape(0x55c8a97a,CARD_RIM,8));
        s.addState(new int[]{},shape(0x00000000,CARD_RIM,8));
        return s;
    }
    /** 节标题：左侧一道金色竖线 + 加大字距的标题，和正文拉开层次。 */
    private void section(LinearLayout body,String title){
        LinearLayout row=new LinearLayout(activity);row.setOrientation(LinearLayout.HORIZONTAL);row.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams rp=new LinearLayout.LayoutParams(-1,-2);rp.topMargin=dp(18);rp.bottomMargin=dp(8);body.addView(row,rp);
        View bar=new View(activity);bar.setBackgroundColor(GOLD);
        row.addView(bar,new LinearLayout.LayoutParams(dp(3),dp(18)));
        TextView label=new TextView(activity);label.setText(title);label.setTextColor(GOLD);label.setTextSize(17);
        label.setLetterSpacing(0.08f);label.setPadding(dp(9),0,0,0);
        row.addView(label,new LinearLayout.LayoutParams(-2,-2));
    }
    /** 一节的卡片容器：把该节的所有控件装进一个描边块里，视觉上成组。 */
    private LinearLayout card(LinearLayout body){
        LinearLayout box=new LinearLayout(activity);box.setOrientation(LinearLayout.VERTICAL);
        box.setBackground(shape(CARD,CARD_RIM,12));box.setPadding(dp(12),dp(10),dp(12),dp(10));
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.bottomMargin=dp(6);body.addView(box,lp);return box;
    }
    /** 分隔线：卡片之间的一道细金线。 */
    private void divider(LinearLayout body){
        View line=new View(activity);line.setBackgroundColor(0x66a08359);
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,Math.max(1,dp(1)));lp.topMargin=dp(14);lp.bottomMargin=dp(2);
        body.addView(line,lp);
    }
    private TextView text(LinearLayout body,String value,int size){
        TextView view=new TextView(activity);view.setText(value);view.setTextColor(TEXT);view.setTextSize(size);
        view.setPadding(0,dp(4),0,dp(8));body.addView(view,new LinearLayout.LayoutParams(-1,-2));return view;
    }
    /** 说明小字：比正文更暗一档，避免和可操作的项抢注意力。 */
    private TextView hint(LinearLayout body,String value){
        TextView view=text(body,value,13);view.setTextColor(MUTED);view.setPadding(dp(2),dp(2),dp(2),dp(6));return view;
    }
    /** 状态行：带一层淡底，读起来像「当前结果」而不是又一条说明。 */
    private TextView statusLine(LinearLayout body,String value){
        TextView view=new TextView(activity);view.setText(value);view.setTextColor(GOLD);view.setTextSize(13);
        view.setBackground(shape(0x2a000000,0x44a08359,8));view.setPadding(dp(10),dp(7),dp(10),dp(7));
        view.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.topMargin=dp(4);body.addView(view,lp);return view;
    }
    private RadioButton qualityOption(RadioGroup group,String label){
        RadioButton control=new RadioButton(activity);control.setId(View.generateViewId());control.setText(label);
        control.setTextColor(TEXT);control.setTextSize(16);control.setMinHeight(dp(44));
        control.setPadding(dp(10),dp(4),dp(10),dp(4));control.setBackground(optionBackground());
        control.setGravity(Gravity.CENTER_VERTICAL);
        control.setButtonTintList(new ColorStateList(new int[][]{new int[]{android.R.attr.state_checked},new int[]{}},new int[]{GOLD,RIM}));
        RadioGroup.LayoutParams lp=new RadioGroup.LayoutParams(-1,-2);lp.bottomMargin=dp(6);
        group.addView(control,lp);return control;
    }
    private void bindQuality(){
        automatic.setChecked(desiredScale==RenderQuality.AUTO);
        original.setChecked(desiredScale==RenderQuality.ORIGINAL);
        balanced.setChecked(desiredScale==RenderQuality.BALANCED);
        performance.setChecked(desiredScale==RenderQuality.PERFORMANCE);
        qualityFeedback();
    }
    private void changeQuality(float scale){
        if(qualityReadFailed)return;
        try{qualityStore.write(scale);desiredScale=scale;
            int rc=scaleRequest.apply(scale);qualityError=rc==0?null:"已保存，暂未应用。请再次选择模式重试。";
        }catch(IOException e){qualityError="保存失败，画质设置未改变。请重新选择重试。";}
        bindQuality();
    }
    private void qualityFeedback(){
        String value=qualityError!=null?qualityError:desiredScale==RenderQuality.AUTO?
            "自动模式已启用\n当前 3D 比例："+Math.round(appliedScale*100)+"%":
            Math.abs(appliedScale-desiredScale)<0.0001f?"已生效："+RenderQuality.description(appliedScale):
            "正在应用…\n当前生效："+RenderQuality.description(appliedScale);
        if(!value.contentEquals(qualityStatus.getText()))qualityStatus.setText(value);
    }
    /* 帧率档位的绑定 / 切换 / 回显。
     * 切换只写存档文件：native 侧的节拍器每帧读一次 diagnostics/present-hz.txt，下一次出图就用新档位，
     * 不需要像画质模式那样再走一次 apply 回调（那条要重建交换链，会短暂停顿）。
     * Bind / switch / feedback for the frame-rate tier. Switching only writes the settings file: the
     * native pacer reads diagnostics/present-hz.txt once per frame, so the next present already uses
     * the new tier — no apply callback like the quality mode, which has to rebuild the swapchain and
     * causes a brief stall. */
    private void bindFps(){
        fps60.setChecked(fpsDesired==60);fps40.setChecked(fpsDesired==40);fps30.setChecked(fpsDesired==30);
        fpsFeedback();
    }
    private void changeFps(int hz){
        if(fpsReadFailed)return;
        try{fpsStore.write(hz);fpsDesired=hz;fpsError=null;}
        catch(IOException e){fpsError="保存失败，帧率未改变。请重新选择重试。";}
        bindFps();
    }
    private void fpsFeedback(){
        String value=fpsError!=null?fpsError:
            "已生效："+fpsDesired+" 帧（下一次出图起）";
        if(!value.contentEquals(fpsStatus.getText()))fpsStatus.setText(value);
    }
    private Switch option(LinearLayout body,String title,String detail){
        Switch control=new Switch(activity);control.setText(title);control.setTextColor(TEXT);control.setTextSize(16);
        control.setMinHeight(dp(46));control.setSwitchPadding(dp(18));
        control.setThumbTintList(new ColorStateList(new int[][]{new int[]{android.R.attr.state_checked},new int[]{}},new int[]{GOLD,RIM}));
        control.setTrackTintList(new ColorStateList(new int[][]{new int[]{android.R.attr.state_checked},new int[]{}},new int[]{0x99d8b173,0x55201511}));
        control.setPadding(dp(4),dp(2),dp(4),dp(2));
        body.addView(control,new LinearLayout.LayoutParams(-1,-2));
        hint(body,detail);return control;
    }
    private void reload(){
        try{desired=store.read();readFailed=false;error=null;bind();}
        catch(IOException e){readFailed=true;error="设置读取失败，返回游戏后重新打开菜单重试。";clear.setEnabled(false);writeback.setEnabled(false);feedback();}
    }
    private void bind(){
        binding=true;clear.setChecked(desired.enabled(RendererOptions.CLEAR_MERGE));writeback.setChecked(desired.enabled(RendererOptions.STORE_ELIDE));binding=false;feedback();
    }
    private void change(int bit,boolean enabled){
        if(binding||readFailed)return;
        try{desired=store.change(bit,enabled);error=null;bind();}
        catch(IOException e){error="保存失败，设置未改变。请重新切换重试。";bind();}
    }
    void updateApplied(int flags,float scale){
        applied=flags;
        appliedScale=scale;qualityFeedback();
        if(!readFailed)try{RendererOptions latest=store.read();if(desired==null||latest.sequence!=desired.sequence){desired=latest;error=null;bind();}}
        catch(IOException e){error="设置暂时无法读取，请返回游戏后重试。";}
        feedback();
    }
    private void feedback(){
        String value=error!=null?error:applied==desired.flags?"已生效："+RendererOptions.description(applied):"正在应用…\n当前生效："+RendererOptions.description(applied);
        if(!value.contentEquals(status.getText()))status.setText(value);
    }
    void dismiss(){dialog.dismiss();}
}
