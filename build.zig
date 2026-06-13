const std = @import("std");

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});
    _ = optimize;

    const ClumsyConf = enum { Debug, Release, Ship };
    const conf = b.option(ClumsyConf, "conf", "Debug, Release, Ship") orelse .Debug;

    const optimize_mode, const subsystem = switch (conf) {
        .Debug => .{ std.builtin.OptimizeMode.Debug, std.Target.SubSystem.Console },
        .Release => .{ std.builtin.OptimizeMode.ReleaseSafe, std.Target.SubSystem.Windows },
        .Ship => .{ std.builtin.OptimizeMode.ReleaseFast, std.Target.SubSystem.Windows },
    };

    const windivert_dir = "WinDivert-2.2.0-A";

    const targets = [_]struct {
        name: []const u8,
        triple: []const u8,
        arch_dir: []const u8,
        windivert_sys: []const u8,
        iup_lib_dir: []const u8,
    }{
        .{
            .name = "x64",
            .triple = "x86_64-windows-gnu",
            .arch_dir = "x64",
            .windivert_sys = "WinDivert64.sys",
            .iup_lib_dir = "external/iup-3.30_Win64_mingw6_lib",
        },
        .{
            .name = "x86",
            .triple = "x86-windows-gnu",
            .arch_dir = "x86",
            .windivert_sys = "WinDivert32.sys",
            .iup_lib_dir = "external/iup-3.30_Win32_mingw6_lib",
        },
    };

    for (targets) |t| {
        const target_query = std.Target.Query.parse(.{ .arch_os_abi = t.triple }) catch unreachable;
        const target = b.resolveTargetQuery(target_query);

        const exe = b.addExecutable(.{
            .name = "clumsy",
            .root_module = b.createModule(.{
                .target = target,
                .optimize = optimize_mode,
                .link_libc = true,
            }),
        });
        exe.subsystem = subsystem;
        const m = exe.root_module;

        // Add C source files
        const c_sources = [_][]const u8{
            "src/bandwidth.c",
            "src/divert.c",
            "src/drop.c",
            "src/duplicate.c",
            "src/elevate.c",
            "src/lag.c",
            "src/main.c",
            "src/ood.c",
            "src/packet.c",
            "src/process_filter.c",
            "src/reset.c",
            "src/tamper.c",
            "src/throttle.c",
            "src/ui_components.c",
            "src/utils.c",
        };

        for (c_sources) |src| {
            m.addCSourceFile(.{ .file = b.path(src), .flags = &.{} });
        }

        if (std.mem.eql(u8, t.name, "x86")) {
            m.addCSourceFile(.{ .file = b.path("etc/chkstk.s"), .flags = &.{} });
        }

        // Add include paths
        m.addIncludePath(b.path(b.fmt("external/{s}/include", .{windivert_dir})));
        m.addIncludePath(b.path(b.fmt("{s}/include", .{t.iup_lib_dir})));

        // Link libiup.a
        m.addObjectFile(b.path(b.fmt("{s}/libiup.a", .{t.iup_lib_dir})));
        
        // Link WinDivert.lib directly as an object file
        m.addObjectFile(b.path(b.fmt("external/{s}/{s}/WinDivert.lib", .{windivert_dir, t.arch_dir})));

        m.linkSystemLibrary("comctl32", .{});
        m.linkSystemLibrary("Winmm", .{});
        m.linkSystemLibrary("ws2_32", .{});
        m.linkSystemLibrary("kernel32", .{});
        m.linkSystemLibrary("gdi32", .{});
        m.linkSystemLibrary("comdlg32", .{});
        m.linkSystemLibrary("uuid", .{});
        m.linkSystemLibrary("ole32", .{});
        m.linkSystemLibrary("iphlpapi", .{});

        // Windows resource file compilation (clumsy.rc)
        m.addWin32ResourceFile(.{ .file = b.path("etc/clumsy.rc") });

        // Setup install step
        const install_exe = b.addInstallArtifact(exe, .{});
        install_exe.dest_sub_path = b.fmt("../{s}/clumsy.exe", .{t.name});
        b.getInstallStep().dependOn(&install_exe.step);

        // Install required DLL, SYS, and config files to zig-out/<arch>/
        b.installFile(b.fmt("external/{s}/{s}/WinDivert.dll", .{windivert_dir, t.arch_dir}), b.fmt("{s}/WinDivert.dll", .{t.name}));
        b.installFile(b.fmt("external/{s}/{s}/{s}", .{windivert_dir, t.arch_dir, t.windivert_sys}), b.fmt("{s}/{s}", .{t.name, t.windivert_sys}));
        b.installFile("etc/config.txt", b.fmt("{s}/config.txt", .{t.name}));
        
        if (conf == .Ship) {
            b.installFile("LICENSE", b.fmt("{s}/License.txt", .{t.name}));
        }
    }
}