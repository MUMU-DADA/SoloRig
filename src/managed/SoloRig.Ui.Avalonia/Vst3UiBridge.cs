using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Themes.Simple;
using Avalonia.Threading;
using SoloRig.Ui;

namespace SoloRig.Ui.Avalonia;

// This bridge is loaded by the native VST3 IPlugView through hostfxr. It
// creates the exact same RigEditorView used by SoloRig.Standalone on a
// dedicated STA/UI thread, then reparents the Avalonia HWND into the host's
// VST3 parent window. No audio callback calls into this bridge.
public static class Vst3UiBridge
{
    private sealed class BridgeState
    {
        public readonly ManualResetEventSlim Ready = new(false);
        public readonly ManualResetEventSlim Stopped = new(false);
        public readonly CancellationTokenSource Cancellation = new();
        public IntPtr Parent;
        public IntPtr Hwnd;
        public int Width;
        public int Height;
        public Window? Window;
        public RigEditorViewModel? ViewModel;
        public string? Error;
    }

    private static readonly object Gate = new();
    private static BridgeState? state;

    [UnmanagedCallersOnly]
    public static IntPtr Start(IntPtr parent, int width, int height)
    {
        try
        {
            return StartCore(parent, Math.Max(width, 320), Math.Max(height, 220));
        }
        catch
        {
            return IntPtr.Zero;
        }
    }

    [UnmanagedCallersOnly]
    public static int Resize(int width, int height)
    {
        BridgeState? current;
        lock (Gate)
        {
            current = state;
            if (current is null || current.Hwnd == IntPtr.Zero)
            {
                return 0;
            }
            current.Width = Math.Max(width, 1);
            current.Height = Math.Max(height, 1);
        }

        Dispatcher.UIThread.Post(() =>
        {
            if (current.Window is not null)
            {
                current.Window.Width = current.Width;
                current.Window.Height = current.Height;
                Native.SetWindowPos(current.Hwnd, IntPtr.Zero, 0, 0, current.Width, current.Height,
                    Native.SwpNoZOrder | Native.SwpNoActivate | Native.SwpShowWindow);
            }
        });
        return 1;
    }

    [UnmanagedCallersOnly]
    public static void Stop()
    {
        BridgeState? current;
        lock (Gate)
        {
            current = state;
            state = null;
        }
        if (current is null)
        {
            return;
        }

        current.Cancellation.Cancel();
        Dispatcher.UIThread.Post(() =>
        {
            current.Window?.Close();
            Dispatcher.UIThread.ExitAllFrames();
        });
        if (!current.Stopped.IsSet)
        {
            current.Stopped.Wait(TimeSpan.FromSeconds(3));
        }
        current.Cancellation.Dispose();
        current.Ready.Dispose();
        current.Stopped.Dispose();
    }

    private static IntPtr StartCore(IntPtr parent, int width, int height)
    {
        lock (Gate)
        {
            if (state is not null && state.Hwnd != IntPtr.Zero)
            {
                return state.Hwnd;
            }

            var next = new BridgeState
            {
                Parent = parent,
                Width = width,
                Height = height
            };
            state = next;
            var thread = new Thread(() => Run(next))
            {
                IsBackground = true,
                Name = "SoloRig Avalonia VST3 UI"
            };
            if (OperatingSystem.IsWindows())
            {
                thread.SetApartmentState(ApartmentState.STA);
            }
            thread.Start();
            if (!next.Ready.Wait(TimeSpan.FromSeconds(10)) || next.Hwnd == IntPtr.Zero)
            {
                state = null;
                next.Cancellation.Cancel();
                return IntPtr.Zero;
            }
            return next.Hwnd;
        }
    }

    private static void Run(BridgeState current)
    {
        try
        {
            BuildAvaloniaApp().SetupWithoutStarting();
            var viewModel = new RigEditorViewModel();
            if (!viewModel.IsBackendAvailable)
            {
                viewModel.Dispose();
                throw new InvalidOperationException("SoloRigBackend.dll could not be loaded beside the shared C# UI assembly");
            }
            var view = new RigEditorView(viewModel);
            var window = new Window
            {
                Width = current.Width,
                Height = current.Height,
                MinWidth = 320,
                MinHeight = 220,
                SystemDecorations = SystemDecorations.None,
                CanResize = false,
                ShowInTaskbar = false,
                WindowStartupLocation = WindowStartupLocation.Manual,
                Background = Brushes.Transparent,
                Content = view
            };
            current.ViewModel = viewModel;
            current.Window = window;
            window.Show();
            var hwnd = window.TryGetPlatformHandle()?.Handle ?? IntPtr.Zero;
            if (hwnd == IntPtr.Zero || current.Parent == IntPtr.Zero)
            {
                current.Error = "Avalonia VST3 bridge did not receive valid HWND handles";
                current.Ready.Set();
                return;
            }
            Native.AttachChild(hwnd, current.Parent, current.Width, current.Height);
            current.Hwnd = hwnd;
            current.Ready.Set();
            Dispatcher.UIThread.MainLoop(current.Cancellation.Token);
        }
        catch (Exception exception)
        {
            current.Error = exception.ToString();
            current.Ready.Set();
        }
        finally
        {
            current.Window?.Close();
            current.ViewModel?.Dispose();
            current.Hwnd = IntPtr.Zero;
            current.Stopped.Set();
        }
    }

    private static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<EmbeddedApp>().UsePlatformDetect().LogToTrace();

    private sealed class EmbeddedApp : Application
    {
        public override void Initialize() => Styles.Add(new SimpleTheme());
    }

    private static class Native
    {
        public const uint SwpNoZOrder = 0x0004;
        public const uint SwpNoActivate = 0x0010;
        public const uint SwpShowWindow = 0x0040;
        private const int GwlStyle = -16;
        private const long WsChild = 0x40000000L;
        private const long WsVisible = 0x10000000L;
        private const long WsPopup = unchecked((long)0x80000000U);

        [DllImport("user32.dll", EntryPoint = "SetParent", ExactSpelling = true)]
        private static extern IntPtr SetParent(IntPtr child, IntPtr parent);

        [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW", ExactSpelling = true)]
        private static extern nint GetWindowLongPtr(IntPtr window, int index);

        [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW", ExactSpelling = true)]
        private static extern nint SetWindowLongPtr(IntPtr window, int index, nint value);

        [DllImport("user32.dll", EntryPoint = "SetWindowPos", ExactSpelling = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter, int x, int y, int width, int height,
            uint flags);

        public static void AttachChild(IntPtr child, IntPtr parent, int width, int height)
        {
            var style = GetWindowLongPtr(child, GwlStyle);
            var childStyle = ((long)style & ~WsPopup) | WsChild | WsVisible;
            SetWindowLongPtr(child, GwlStyle, (nint)childStyle);
            SetParent(child, parent);
            SetWindowPos(child, IntPtr.Zero, 0, 0, width, height, SwpNoZOrder | SwpNoActivate | SwpShowWindow);
        }
    }
}
