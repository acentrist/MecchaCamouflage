using MecchaCamouflage.Controller;
using MecchaCamouflage.Core;
using System.Text.Json;

namespace MecchaCamouflage.WebHost;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        var paths = new MecchaCamouflage.Core.AppPaths(VersionInfo.Current);
        var startupCatalog = LocalizationCatalog.Load();
        var startupLocale = LocalizationCatalog.DetectSystemLanguage();
        DiagnosticsState.Initialize(paths, VersionInfo.Current);
        var captureBodyType = CaptureReferenceBodyType(args);
        if (captureBodyType is not null)
        {
            Environment.ExitCode = CaptureImageReferencePoseAsync(captureBodyType).GetAwaiter().GetResult();
            return;
        }
#if MECCHA_RESEARCH_BUILD
        if (ResearchRunner.IsRequested(args))
        {
            Environment.ExitCode = ResearchRunner.RunAsync(args).GetAwaiter().GetResult();
            return;
        }
#endif
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += (_, args) => DiagnosticsState.RecordException("winforms_thread_exception", args.Exception);
        AppDomain.CurrentDomain.UnhandledException += (_, args) =>
        {
            if (args.ExceptionObject is Exception exception)
                DiagnosticsState.RecordException("appdomain_unhandled_exception", exception);
        };
        TaskScheduler.UnobservedTaskException += (_, args) =>
        {
            DiagnosticsState.RecordException("task_unobserved_exception", args.Exception);
            args.SetObserved();
        };

        try
        {
            DiagnosticsState.SetStartupPhase("application_configuration");
            ApplicationConfiguration.Initialize();
            DiagnosticsState.SetStartupPhase("main_form_create");
            using var form = new MainForm(new HostSession(VersionInfo.Current, ReadDiagnosticStrokeLimit(args)));
            DiagnosticsState.SetStartupPhase("application_run");
            Application.Run(form);
            DiagnosticsState.SetStartupPhase("application_exit");
        }
        catch (Exception exception)
        {
            DiagnosticsState.RecordException("application_run_failed", exception);
            MessageBox.Show(
                startupCatalog.Text(startupLocale, "dialog.startup.failed") +
                Environment.NewLine + paths.DiagnosticsDirectory +
                Environment.NewLine + Environment.NewLine + exception.Message,
                startupCatalog.Text(startupLocale, "app.title"),
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
        }
    }

    private static string? CaptureReferenceBodyType(string[] args)
    {
        if (args.Any(argument => string.Equals(argument, "--capture-cube-reference-pose", StringComparison.Ordinal)))
            return "cube";
        if (args.Any(argument => string.Equals(argument, "--capture-round-reference-pose", StringComparison.Ordinal)))
            return "round";
        return null;
    }

    private static async Task<int> CaptureImageReferencePoseAsync(string bodyType)
    {
        var session = new HostSession(VersionInfo.Current);
        try
        {
            var snapshot = await session.CaptureImageReferencePoseAsync(bodyType);
            Console.Out.WriteLine(JsonSerializer.Serialize(snapshot));
            return snapshot.Success ? 0 : 1;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine("Image reference pose capture failed: " + exception.Message);
            return 1;
        }
        finally
        {
            try
            {
                await session.Runtime.ShutdownAsync();
            }
            catch
            {
                // Capture cleanup must not hide its result.
            }
        }
    }

    private static int ReadDiagnosticStrokeLimit(string[] args)
    {
        for (var index = 0; index < args.Length; ++index)
        {
            if (!string.Equals(args[index], "--diagnostic-stroke-limit", StringComparison.Ordinal))
                continue;
            if (++index >= args.Length ||
                !int.TryParse(args[index], out var value) ||
                value is < 1 or > 10_000)
            {
                throw new ArgumentException("--diagnostic-stroke-limit must be an integer from 1 through 10000.");
            }
            return value;
        }
        return 0;
    }
}
