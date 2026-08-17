namespace SoloRig.Ui;

public sealed record AuditionWaveFileSnapshot(
    string SourcePath,
    uint SampleRateHz,
    uint SourceChannelCount,
    uint FrameCount,
    string Encoding,
    IReadOnlyList<float> MonoSamples);

public static class AuditionWaveFile
{
    private const long MaximumFileBytes = 64L * 1024L * 1024L;
    private const uint MinimumFrames = 16;
    private const uint MaximumFrames = 8192;

    public static AuditionWaveFileSnapshot Load(string path)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            throw new FileNotFoundException("试听 WAV 文件不存在", path);
        }
        if (!string.Equals(Path.GetExtension(path), ".wav", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("文件试听只接受 .wav 文件");
        }

        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read,
            bufferSize: 4096, FileOptions.SequentialScan);
        if (stream.Length is < 12 or > MaximumFileBytes)
        {
            throw new InvalidDataException("WAV 文件大小无效或超过 64 MB");
        }
        using var reader = new BinaryReader(stream);
        if (!ReadFourCc(reader, "RIFF"))
        {
            throw new InvalidDataException("文件不是 RIFF/WAVE 容器");
        }
        _ = reader.ReadUInt32();
        if (!ReadFourCc(reader, "WAVE"))
        {
            throw new InvalidDataException("文件不是 RIFF/WAVE 容器");
        }

        ushort format = 0;
        ushort channels = 0;
        uint sampleRate = 0;
        ushort blockAlign = 0;
        ushort bitDepth = 0;
        long dataOffset = -1;
        uint dataSize = 0;
        var foundFormat = false;
        while (stream.Position + 8 <= stream.Length)
        {
            var chunkId = reader.ReadBytes(4);
            if (chunkId.Length != 4)
            {
                throw new InvalidDataException("WAV 区块头不完整");
            }
            var chunkSize = reader.ReadUInt32();
            var chunkStart = stream.Position;
            var chunkEnd = checked(chunkStart + chunkSize);
            if (chunkEnd > stream.Length)
            {
                throw new InvalidDataException("WAV 区块长度越界");
            }
            if (MatchesFourCc(chunkId, "fmt "))
            {
                if (foundFormat || chunkSize < 16)
                {
                    throw new InvalidDataException("WAV fmt 区块无效");
                }
                format = reader.ReadUInt16();
                channels = reader.ReadUInt16();
                sampleRate = reader.ReadUInt32();
                _ = reader.ReadUInt32();
                blockAlign = reader.ReadUInt16();
                bitDepth = reader.ReadUInt16();
                foundFormat = true;
            }
            else if (MatchesFourCc(chunkId, "data") && dataOffset < 0)
            {
                dataOffset = chunkStart;
                dataSize = chunkSize;
            }
            stream.Position = chunkEnd + (chunkSize & 1U);
        }

        if (!foundFormat || dataOffset < 0)
        {
            throw new InvalidDataException("WAV 缺少 fmt 或 data 区块");
        }
        if (channels is < 1 or > 2)
        {
            throw new InvalidDataException("文件试听只支持单声道或双声道 WAV");
        }
        if (sampleRate is < 8000 or > 384000)
        {
            throw new InvalidDataException("WAV 采样率必须在 8 kHz 到 384 kHz 之间");
        }
        var pcm = format == 1;
        var float32 = format == 3 && bitDepth == 32;
        if (!pcm && !float32)
        {
            throw new InvalidDataException("只支持 PCM 16/24/32 或 IEEE Float32 WAV");
        }
        if (pcm && bitDepth is not (16 or 24 or 32))
        {
            throw new InvalidDataException("PCM WAV 只支持 16、24 或 32 bit");
        }
        var bytesPerSample = bitDepth / 8;
        if (blockAlign != channels * bytesPerSample || dataSize == 0 || dataSize % blockAlign != 0)
        {
            throw new InvalidDataException("WAV 声道布局或数据长度无效");
        }
        var frameCount = dataSize / blockAlign;
        if (frameCount is < MinimumFrames or > MaximumFrames)
        {
            throw new InvalidDataException($"文件试听长度必须为 {MinimumFrames} 到 {MaximumFrames} 帧");
        }

        stream.Position = dataOffset;
        var samples = new float[frameCount];
        for (var frame = 0; frame < frameCount; ++frame)
        {
            double sum = 0.0;
            for (var channel = 0; channel < channels; ++channel)
            {
                sum += ReadSample(reader, pcm, bitDepth);
            }
            var sample = (float)(sum / channels);
            if (!float.IsFinite(sample))
            {
                throw new InvalidDataException("WAV 包含非有限 Float32 样本");
            }
            samples[frame] = sample;
        }

        var encoding = float32 ? "IEEE Float32" : $"PCM {bitDepth}";
        return new AuditionWaveFileSnapshot(Path.GetFullPath(path), sampleRate, channels, frameCount,
            encoding, samples);
    }

    private static float ReadSample(BinaryReader reader, bool pcm, ushort bitDepth)
    {
        if (!pcm)
        {
            return BitConverter.Int32BitsToSingle(reader.ReadInt32());
        }
        return bitDepth switch
        {
            16 => reader.ReadInt16() / 32768.0F,
            24 => ReadPcm24(reader) / 8388608.0F,
            32 => reader.ReadInt32() / 2147483648.0F,
            _ => throw new InvalidDataException("不支持的 WAV 位深"),
        };
    }

    private static int ReadPcm24(BinaryReader reader)
    {
        var value = reader.ReadByte() | reader.ReadByte() << 8 | reader.ReadByte() << 16;
        return (value & 0x00800000) == 0 ? value : value | unchecked((int)0xFF000000);
    }

    private static bool ReadFourCc(BinaryReader reader, string expected) =>
        MatchesFourCc(reader.ReadBytes(4), expected);

    private static bool MatchesFourCc(IReadOnlyList<byte> bytes, string expected) =>
        bytes.Count == 4 && bytes[0] == expected[0] && bytes[1] == expected[1] &&
        bytes[2] == expected[2] && bytes[3] == expected[3];
}
