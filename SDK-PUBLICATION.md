# SDK publication status

Current source layout and registry coverage. A GitHub download is distinct from publication on a central registry.

| Target | Status | Package / documentation |
| --- | --- | --- |
| CPP/vcpkg | published_custom_registry | user-backed-virtual-disk-sdk |
| CPP/conan | public_recipe_builds_locally |  [Source](packaging/native/) |
| CPP/nuget | paused_pending_owner_email_confirmation | User-Backed-Virtual-Disk-SDK.Native |
| NET/nuget | built_verified_publication_on_hold_by_owner | UserBackedVirtualDiskSDK.SDK [Source](User-Backed-Virtual-Disk-SDK-NET-nuget/) |
| Python/pypi | built_fixture_and_production_verified | user-backed-virtual-disk-sdk [Source](User-Backed-Virtual-Disk-SDK-Python-pypi/) |
| Python/conda-forge | built_tested_requires_source_publication_and_official_review | user-backed-virtual-disk-sdk [Source](packaging/conda/) |
| Rust/cargo | published_install_verified | [user-backed-virtual-disk-sdk](https://crates.io/crates/user-backed-virtual-disk-sdk) [Source](User-Backed-Virtual-Disk-SDK-Rust-cargo/) |
| JavaScript/npm | awaiting_browser_publication_authentication | [user-backed-virtual-disk-sdk](https://www.npmjs.com/package/user-backed-virtual-disk-sdk) [Source](User-Backed-Virtual-Disk-SDK-JavaScript-npm/) |
| Java/maven | published_install_verified | [user-backed-virtual-disk-sdk](https://central.sonatype.com/artifact/io.github.titancsa/user-backed-virtual-disk-sdk/1.0.0) [Source](User-Backed-Virtual-Disk-SDK-Java-maven/) |
| Java/jitpack | published_install_verified | [user-backed-virtual-disk-sdk](https://jitpack.io/#titanfyre/User-Backed-Virtual-Disk-SDK/java-sdk-v1.0.0) [Source](User-Backed-Virtual-Disk-SDK-Java-maven/) |
| Go/modules | published_install_verified | [user-backed-virtual-disk-sdk](https://pkg.go.dev/github.com/titanfyre/User-Backed-Virtual-Disk-SDK/User-Backed-Virtual-Disk-SDK-Go-modules@v1.0.0) [Source](User-Backed-Virtual-Disk-SDK-Go-modules/) |
| Go/pkg-go-dev | indexed_verified | [Documentation](https://pkg.go.dev/github.com/titanfyre/User-Backed-Virtual-Disk-SDK/User-Backed-Virtual-Disk-SDK-Go-modules@v1.0.0) |
| PowerShell/gallery | published_install_verified | [UserBackedVirtualDiskSDK](https://www.powershellgallery.com/packages/UserBackedVirtualDiskSDK/1.0.0) [Source](User-Backed-Virtual-Disk-SDK-PowerShell-gallery/) |
| Dart/pub-dev | built_fixture_and_production_verified | user_backed_virtual_disk_sdk [Source](User-Backed-Virtual-Disk-SDK-Dart-pub-dev/) |
| Ruby/rubygems | published_install_verified | [user-backed-virtual-disk-sdk](https://rubygems.org/gems/user-backed-virtual-disk-sdk) [Source](User-Backed-Virtual-Disk-SDK-Ruby-rubygems/) |
| PHP/packagist | composer_source_and_tag_published | [titancsa/user-backed-virtual-disk-sdk](https://packagist.org/packages/titancsa/user-backed-virtual-disk-sdk) |

Native SDK source and CMake/Conan/vcpkg/NuGet recipes share [`packaging/native`](packaging/native/LAYOUT.md). Previous release archives and tags remain unchanged.

Application and driver prerequisites and original licensing remain those documented by each SDK. No driver installer is bundled with these source packages.
