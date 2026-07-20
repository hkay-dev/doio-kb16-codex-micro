using System.Windows;

namespace Doio.Kb16.Mapper;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        _ = AppIdentity.ApplyToCurrentProcess();
        base.OnStartup(e);
    }
}
