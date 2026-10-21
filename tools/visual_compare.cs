// Visual compare for tools/visual_check.ps1: one reference screenshot against one of ours.
// Compiled by PowerShell 5.1's Add-Type (C# 5: no string interpolation, no out var).
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public class VcRegion
{
	public int Index, X, Y, W, H, Pixels;
	public int ShiftX, ShiftY;		// ours is offset by this much from the reference (0,0: not a shift)
	public string Kind;				// shifted, missing, extra, differs
	public string Text;
}

public class VcResult
{
	public string Error;
	public int Compared, Masked, Differing;
	public List<VcRegion> Regions = new List<VcRegion>();
}

public static class VisualCompare
{
	const int Cell = 8;

	static int[] Load(string path, out int w, out int h)
	{
		using (Bitmap src = new Bitmap(path)) {
			w = src.Width; h = src.Height;
			using (Bitmap b = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
				using (Graphics g = Graphics.FromImage(b)) { g.DrawImage(src, 0, 0, w, h); }
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

	static Bitmap ToBitmap(int[] px, int w, int h)
	{
		Bitmap b = new Bitmap(w, h, PixelFormat.Format32bppArgb);
		BitmapData d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
		for (int y = 0; y < h; y++) {
			Marshal.Copy(px, y * w, new IntPtr(d.Scan0.ToInt64() + (long)y * d.Stride), w);
		}
		b.UnlockBits(d);
		return b;
	}

	static int R(int c) { return (c >> 16) & 255; }
	static int G(int c) { return (c >> 8) & 255; }
	static int B(int c) { return c & 255; }
	static int Lum(int c) { return (R(c) * 3 + G(c) * 6 + B(c)) / 10; }

	static int PixDiff(int a, int b)
	{
		int d = Math.Abs(R(a) - R(b));
		d = Math.Max(d, Math.Abs(G(a) - G(b)));
		return Math.Max(d, Math.Abs(B(a) - B(b)));
	}

	static string Where(int x, int y, int w, int h, int sw, int sh)
	{
		int cx = x + w / 2, cy = y + h / 2;
		string v = (cy < sh / 3) ? "top" : (cy < 2 * sh / 3) ? "middle" : "bottom";
		string u = (cx < sw / 3) ? "left" : (cx < 2 * sw / 3) ? "centre" : "right";
		if (v == "middle" && u == "centre") return "centre";
		return v + " " + u;
	}

	// rects: x,y,w,h, ignored.  only: if given, everything outside these is ignored.
	// maskPng: optional, any pixel brighter than mid grey is ignored.
	public static VcResult Compare(string refPath, string ourPath, int[][] rects, int[][] only, string maskPng,
		int threshold, int minPixels, int maxShift, string diffOut)
	{
		VcResult res = new VcResult();
		int w, h, ow, oh;
		int[] rp = Load(refPath, out w, out h);
		int[] op = Load(ourPath, out ow, out oh);
		if (ow != w || oh != h) {
			res.Error = "size differs: reference " + w + "x" + h + ", ours " + ow + "x" + oh;
			return res;
		}

		bool[] mask = new bool[w * h];
		if (only != null && only.Length > 0) {
			for (int i = 0; i < w * h; i++) mask[i] = true;
			foreach (int[] r in only) {
				for (int y = Math.Max(0, r[1]); y < Math.Min(h, r[1] + r[3]); y++)
					for (int x = Math.Max(0, r[0]); x < Math.Min(w, r[0] + r[2]); x++)
						mask[y * w + x] = false;
			}
		}
		if (rects != null) {
			foreach (int[] r in rects) {
				for (int y = Math.Max(0, r[1]); y < Math.Min(h, r[1] + r[3]); y++)
					for (int x = Math.Max(0, r[0]); x < Math.Min(w, r[0] + r[2]); x++)
						mask[y * w + x] = true;
			}
		}
		if (!string.IsNullOrEmpty(maskPng)) {
			int mw, mh;
			int[] mp = Load(maskPng, out mw, out mh);
			for (int y = 0; y < Math.Min(h, mh); y++)
				for (int x = 0; x < Math.Min(w, mw); x++)
					if (Lum(mp[y * mw + x]) > 127) mask[y * w + x] = true;
		}

		bool[] diff = new bool[w * h];
		int cw = (w + Cell - 1) / Cell, ch = (h + Cell - 1) / Cell;
		int[] cellCount = new int[cw * ch];
		for (int i = 0; i < w * h; i++) {
			if (mask[i]) { res.Masked++; continue; }
			res.Compared++;
			if (PixDiff(rp[i], op[i]) > threshold) {
				diff[i] = true;
				res.Differing++;
				cellCount[(i / w / Cell) * cw + (i % w) / Cell]++;
			}
		}

		// group cells holding differing pixels; cells one apart join the same region
		int[] label = new int[cw * ch];
		int next = 0;
		List<int[]> boxes = new List<int[]>();	// x0,y0,x1,y1,pixels
		Stack<int> stack = new Stack<int>();
		for (int c = 0; c < cw * ch; c++) {
			if (cellCount[c] == 0 || label[c] != 0) continue;
			next++;
			label[c] = next;
			stack.Push(c);
			int[] box = new int[] { int.MaxValue, int.MaxValue, -1, -1, 0 };
			while (stack.Count > 0) {
				int cc = stack.Pop();
				int cx = cc % cw, cy = cc / cw;
				for (int y = cy * Cell; y < Math.Min(h, cy * Cell + Cell); y++)
					for (int x = cx * Cell; x < Math.Min(w, cx * Cell + Cell); x++)
						if (diff[y * w + x]) {
							box[0] = Math.Min(box[0], x); box[1] = Math.Min(box[1], y);
							box[2] = Math.Max(box[2], x); box[3] = Math.Max(box[3], y);
							box[4]++;
						}
				for (int ny = cy - 2; ny <= cy + 2; ny++)
					for (int nx = cx - 2; nx <= cx + 2; nx++) {
						if (nx < 0 || ny < 0 || nx >= cw || ny >= ch) continue;
						int n = ny * cw + nx;
						if (cellCount[n] > 0 && label[n] == 0) { label[n] = next; stack.Push(n); }
					}
			}
			if (box[4] >= minPixels) boxes.Add(box);
		}
		boxes.Sort(delegate(int[] a, int[] b) { return (a[1] != b[1]) ? a[1].CompareTo(b[1]) : a[0].CompareTo(b[0]); });

		foreach (int[] bx in boxes) {
			VcRegion g = new VcRegion();
			g.Index = res.Regions.Count + 1;
			g.X = bx[0]; g.Y = bx[1]; g.W = bx[2] - bx[0] + 1; g.H = bx[3] - bx[1] + 1; g.Pixels = bx[4];
			Classify(g, rp, op, mask, w, h, maxShift);
			g.Text = Describe(g, w, h);
			res.Regions.Add(g);
		}

		if (!string.IsNullOrEmpty(diffOut)) WriteDiff(diffOut, rp, op, mask, diff, w, h, res.Regions);
		return res;
	}

	static void Classify(VcRegion g, int[] rp, int[] op, bool[] mask, int w, int h, int maxShift)
	{
		int pad = 2;
		int x0 = Math.Max(0, g.X - pad), y0 = Math.Max(0, g.Y - pad);
		int x1 = Math.Min(w - 1, g.X + g.W - 1 + pad), y1 = Math.Min(h - 1, g.Y + g.H - 1 + pad);

		// does ours match the reference if moved?  Sum of differences over the region, ours sampled at an offset
		long best = long.MaxValue, at0 = 0;
		int bdx = 0, bdy = 0;
		for (int dy = -maxShift; dy <= maxShift; dy++)
			for (int dx = -maxShift; dx <= maxShift; dx++) {
				long s = 0;
				for (int y = y0; y <= y1 && s < best; y++) {
					int yy = y + dy;
					for (int x = x0; x <= x1; x++) {
						int i = y * w + x;
						if (mask[i]) continue;
						int xx = x + dx;
						int o = (xx < 0 || yy < 0 || xx >= w || yy >= h) ? 0 : op[yy * w + xx];
						s += PixDiff(rp[i], o);
					}
				}
				if (dx == 0 && dy == 0) at0 = s;
				if (s < best || (s == best && Math.Abs(dx) + Math.Abs(dy) < Math.Abs(bdx) + Math.Abs(bdy))) { best = s; bdx = dx; bdy = dy; }
			}
		if ((bdx != 0 || bdy != 0) && best * 3 < at0) {
			g.Kind = "shifted"; g.ShiftX = bdx; g.ShiftY = bdy;
			return;
		}

		// content in one and not the other: count lit pixels (anything above a dark background)
		int refLit = 0, ourLit = 0;
		for (int y = g.Y; y < g.Y + g.H; y++)
			for (int x = g.X; x < g.X + g.W; x++) {
				int i = y * w + x;
				if (mask[i]) continue;
				if (Lum(rp[i]) > 48) refLit++;
				if (Lum(op[i]) > 48) ourLit++;
			}
		if (refLit > 20 && ourLit * 4 < refLit) g.Kind = "missing";
		else if (ourLit > 20 && refLit * 4 < ourLit) g.Kind = "extra";
		else g.Kind = "differs";
	}

	static string Describe(VcRegion g, int sw, int sh)
	{
		string where = Where(g.X, g.Y, g.W, g.H, sw, sh);
		string rect = "x " + g.X + ", y " + g.Y + ", " + g.W + "x" + g.H;
		switch (g.Kind) {
			case "shifted":
				List<string> parts = new List<string>();
				if (g.ShiftX != 0) parts.Add(Math.Abs(g.ShiftX) + " px " + (g.ShiftX > 0 ? "right" : "left"));
				if (g.ShiftY != 0) parts.Add(Math.Abs(g.ShiftY) + " px " + (g.ShiftY > 0 ? "down" : "up"));
				return "ours is " + string.Join(", ", parts.ToArray()) + " of the reference (" + where + ", " + rect + ")";
			case "missing":
				return "drawn in the reference, missing in ours (" + where + ", " + rect + ")";
			case "extra":
				return "drawn in ours, not in the reference (" + where + ", " + rect + ")";
			default:
				return "art or text differs (" + where + ", " + rect + ", " + g.Pixels + " px)";
		}
	}

	// three panels: reference | ours | difference.  Differing pixels red, ignored areas blue, regions boxed and numbered
	static void WriteDiff(string path, int[] rp, int[] op, bool[] mask, bool[] diff, int w, int h, List<VcRegion> regions)
	{
		int[] dp = new int[w * h];
		for (int i = 0; i < w * h; i++) {
			int l = Lum(rp[i]) / 3;
			if (mask[i]) dp[i] = unchecked((int)0xFF000000) | (l << 16) | (l << 8) | Math.Min(255, l + 90);
			else if (diff[i]) dp[i] = unchecked((int)0xFFFF2020);
			else dp[i] = unchecked((int)0xFF000000) | (l << 16) | (l << 8) | l;
		}
		using (Bitmap outb = new Bitmap(w * 3, h + 18, PixelFormat.Format32bppArgb))
		using (Graphics g = Graphics.FromImage(outb))
		using (Bitmap a = ToBitmap(rp, w, h))
		using (Bitmap b = ToBitmap(op, w, h))
		using (Bitmap c = ToBitmap(dp, w, h))
		using (Font f = new Font("Arial", 9, FontStyle.Bold))
		using (Pen pen = new Pen(Color.Yellow, 1)) {
			g.Clear(Color.Black);
			g.DrawImage(a, 0, 18, w, h);
			g.DrawImage(b, w, 18, w, h);
			g.DrawImage(c, w * 2, 18, w, h);
			g.DrawString("reference", f, Brushes.White, 4, 2);
			g.DrawString("ours", f, Brushes.White, w + 4, 2);
			g.DrawString("difference (red), ignored (blue)", f, Brushes.White, 2 * w + 4, 2);
			foreach (VcRegion r in regions) {
				for (int p = 0; p < 3; p++) {
					g.DrawRectangle(pen, p * w + r.X - 2, 18 + r.Y - 2, r.W + 3, r.H + 3);
					g.DrawString(r.Index.ToString(), f, Brushes.Yellow, p * w + r.X - 2, 18 + Math.Max(0, r.Y - 15));
				}
			}
			outb.Save(path, ImageFormat.Png);
		}
	}

	// our screenshot (any format .NET reads, BMP from the game) to PNG
	public static void ToPng(string src, string dst)
	{
		using (Bitmap b = new Bitmap(src)) { b.Save(dst, ImageFormat.Png); }
	}
}
