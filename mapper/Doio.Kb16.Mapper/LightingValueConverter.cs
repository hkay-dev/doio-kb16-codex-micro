namespace Doio.Kb16.Mapper;

public static class LightingValueConverter
{
    public static byte PercentToByte(double percent) =>
        (byte)Math.Clamp((int)Math.Round(percent * 255d / 100d), 0, 255);

    public static int ByteToPercent(byte value) =>
        (int)Math.Round(value * 100d / 255d);

    public static byte DegreesToHueByte(double degrees) =>
        (byte)Math.Clamp((int)Math.Round(degrees * 255d / 359d), 0, 255);

    public static int HueByteToDegrees(byte hue) =>
        (int)Math.Round(hue * 359d / 255d);
}
