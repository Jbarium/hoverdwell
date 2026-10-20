package com.bariumana.hoverdwell;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;

public class MenuView extends View {
    final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    float cx, cy, r, ri, prog;
    String[] labels = new String[0];
    int hl = -1;
    ValueAnimator anim;

    public MenuView(Context c) { super(c); }

    void set(float cx, float cy, float r, float ri, String[] labels, int hl, long selMs) {
        this.cx = cx; this.cy = cy; this.r = r; this.ri = ri; this.labels = labels;
        if (hl != this.hl) {
            this.hl = hl;
            if (anim != null) anim.cancel();
            prog = 0;
            if (hl >= 0) {            // progress ring fills while the finger dwells on the item
                anim = ValueAnimator.ofFloat(0f, 1f);
                anim.setDuration(selMs);
                anim.addUpdateListener(a -> { prog = (float) a.getAnimatedValue(); invalidate(); });
                anim.start();
            }
        }
        invalidate();
    }

    @Override protected void onDraw(Canvas c) {
        c.drawColor(0x66000000);
        float sw = ri * 0.08f;

        // center = cancel
        p.setStyle(Paint.Style.FILL); p.setColor(0xCC222222);
        c.drawCircle(cx, cy, ri * 0.8f, p);
        p.setStyle(Paint.Style.STROKE); p.setColor(0xFFFFFFFF); p.setStrokeWidth(sw);
        float k = ri * 0.28f;
        c.drawLine(cx - k, cy - k, cx + k, cy + k, p);
        c.drawLine(cx - k, cy + k, cx + k, cy - k, p);

        int n = labels.length;
        for (int i = 0; i < n; i++) {
            double a = -Math.PI / 2 + 2 * Math.PI * i / n;
            float x = cx + (float) (r * Math.cos(a)), y = cy + (float) (r * Math.sin(a));
            p.setStyle(Paint.Style.FILL);
            p.setColor(i == hl ? 0xFF2979FF : 0xE0303030);
            c.drawCircle(x, y, ri, p);
            p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(sw); p.setColor(0xFFFFFFFF);
            c.drawCircle(x, y, ri, p);
            if (i == hl && prog > 0) {
                p.setColor(0xFF00E5FF); p.setStrokeWidth(sw * 2);
                c.drawArc(new RectF(x - ri, y - ri, x + ri, y + ri), -90, 360 * prog, false, p);
            }
            p.setStyle(Paint.Style.FILL); p.setColor(0xFFFFFFFF);
            p.setTextAlign(Paint.Align.CENTER);
            float ts = ri * 0.36f;
            p.setTextSize(ts);
            float w = p.measureText(labels[i]);
            if (w > ri * 1.7f) { ts *= ri * 1.7f / w; p.setTextSize(ts); }
            c.drawText(labels[i], x, y + ts * 0.35f, p);
        }
    }
}