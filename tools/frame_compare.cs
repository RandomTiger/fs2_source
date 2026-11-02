// Graded pixel compare of two frame captures (tools/frame_compare.ps1, docs/VISUAL_COMPARE.md).
// Compiled by PowerShell 5.1's Add-Type (C# 5: no string interpolation, no out var).
// Both pictures must have the same size: a different size is reported, never scaled.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public class FcResult
{
	public string Error;
	public int Width, Height;
	public int Compared;		// pixels compared (not masked)
	public int Differing;		// pixels with any channel different
	public int Over;			// pixels with a channel differing by more than the threshold
	public int MaxDelta;		// largest channel difference, 0-255
	public double MeanDelta;	// mean channel difference over compared pixels
	public double PctOver;		// Over / Compared * 100
	public double Psnr;			// dB over RGB; PositiveInfinity when identical
	public double Ssim;			// mean SSIM of luminance over 8x8 blocks, 1 when identical
}

public static class FrameCompare
{
	static int[] Load(string path, out int w, out int h)
	{
		using (Bitmap src = new Bitmap(path)) {
			w = src.Width; h = src.Height;
			using (Bitmap b = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
				using (Graphics g = Graphics.FromImage(b)) { g.DrawImageUnscaled(src, 0, 0); }
				BitmapData d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
				int[] px = new int[w * h];
				for (int y = 0; y < h; y++) {
					Marshal.Copy(new IntPtr(d.Scan0.ToInt64() + (long)y * d.Stride), px, y * w, w);
				}
				b.UnlockBits(d);
				return px;
			}
		}
	}

	static void Save(int[] px, int w, int h, string path)
	{
		using (Bitmap b = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
			BitmapData d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
			for (int y = 0; y < h; y++) {
				Marshal.Copy(px, y * w, new IntPtr(d.Scan0.ToInt64() + (long)y * d.Stride), w);
			}
			b.UnlockBits(d);
			b.Save(path, ImageFormat.Png);
		}
	}

	static double Lum(int c) { return 0.299 * ((c >> 16) & 255) + 0.587 * ((c >> 8) & 255) + 0.114 * (c & 255); }

	// masks: x,y,w,h per rectangle (may be null).  threshold: a channel difference above it counts in Over.
	// diffOut: when not null and the pictures differ, a PNG of three panels: a, b, and the difference
	// (black = same, grey = at most the threshold, red scaled by the difference above it, blue = masked).
	public static FcResult Compare(string a, string b, int[] masks, int threshold, string diffOut)
	{
		FcResult r = new FcResult();
		int w, h, w2, h2;
		int[] pa, pb;
		try { pa = Load(a, out w, out h); pb = Load(b, out w2, out h2); }
		catch (Exception e) { r.Error = e.Message; return r; }
		r.Width = w; r.Height = h;
		if ((w != w2) || (h != h2)) { r.Error = "size " + w + "x" + h + " vs " + w2 + "x" + h2; return r; }

		bool[] masked = new bool[w * h];
		if (masks != null) {
			for (int m = 0; m + 3 < masks.Length; m += 4) {
				for (int y = Math.Max(0, masks[m + 1]); y < Math.Min(h, masks[m + 1] + masks[m + 3]); y++) {
					for (int x = Math.Max(0, masks[m]); x < Math.Min(w, masks[m] + masks[m + 2]); x++) masked[y * w + x] = true;
				}
			}
		}

		int[] dimg = (diffOut != null) ? new int[w * h] : null;
		long sumAbs = 0; double sumSq = 0;
		for (int i = 0; i < w * h; i++) {
			if (masked[i]) { if (dimg != null) dimg[i] = unchecked((int)0xFF000080); continue; }
			r.Compared++;
			int ca = pa[i], cb = pb[i];
			int d = 0;
			for (int s = 0; s <= 16; s += 8) {
				int e = Math.Abs(((ca >> s) & 255) - ((cb >> s) & 255));
				sumAbs += e; sumSq += (double)e * e;
				if (e > d) d = e;
			}
			if (d > 0) r.Differing++;
			if (d > threshold) r.Over++;
			if (d > r.MaxDelta) r.MaxDelta = d;
			if (dimg != null) {
				int v;
				if (d == 0) v = unchecked((int)0xFF000000);
				else if (d <= threshold) v = unchecked((int)0xFF404040);
				else { int red = 96 + Math.Min(159, (d - threshold) * 2); v = unchecked((int)0xFF000000) | (red << 16); }
				dimg[i] = v;
			}
		}
		if (r.Compared > 0) {
			r.MeanDelta = sumAbs / (3.0 * r.Compared);
			r.PctOver = 100.0 * r.Over / r.Compared;
			double mse = sumSq / (3.0 * r.Compared);
			r.Psnr = (mse == 0) ? double.PositiveInfinity : 10.0 * Math.Log10(255.0 * 255.0 / mse);
		}
		r.Ssim = Ssim(pa, pb, masked, w, h);

		if ((dimg != null) && (r.Differing > 0)) {
			int[] three = new int[w * 3 * h];
			for (int y = 0; y < h; y++) {
				Array.Copy(pa, y * w, three, y * w * 3, w);
				Array.Copy(pb, y * w, three, y * w * 3 + w, w);
				Array.Copy(dimg, y * w, three, y * w * 3 + 2 * w, w);
			}
			Save(three, w * 3, h, diffOut);
		}
		return r;
	}

	// mean SSIM of luminance over 8x8 blocks that are not masked (the usual constants for 8-bit pictures)
	static double Ssim(int[] pa, int[] pb, bool[] masked, int w, int h)
	{
		const double C1 = 6.5025, C2 = 58.5225;
		double total = 0; int blocks = 0;
		for (int by = 0; by + 8 <= h; by += 8) {
			for (int bx = 0; bx + 8 <= w; bx += 8) {
				double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0; int n = 0;
				for (int y = by; y < by + 8; y++) {
					for (int x = bx; x < bx + 8; x++) {
						int i = y * w + x;
						if (masked[i]) continue;
						double la = Lum(pa[i]), lb = Lum(pb[i]);
						sa += la; sb += lb; saa += la * la; sbb += lb * lb; sab += la * lb; n++;
					}
				}
				if (n < 32) continue;
				double ma = sa / n, mb = sb / n;
				double va = saa / n - ma * ma, vb = sbb / n - mb * mb, cov = sab / n - ma * mb;
				total += ((2 * ma * mb + C1) * (2 * cov + C2)) / ((ma * ma + mb * mb + C1) * (va + vb + C2));
				blocks++;
			}
		}
		return (blocks > 0) ? total / blocks : 1.0;
	}
}
