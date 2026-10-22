# Code signing policy

**Current status: preparation only. SignPath Foundation has not accepted this project, and no files are signed through SignPath.**

SignPath Foundation conditions: https://signpath.org/
Free code signing provided by SignPath.io, certificate by [SignPath Foundation](https://signpath.org/) **if approved**. This acknowledgment is not a claim that signing has been granted.

## Source and binary provenance

The trusted source repository is intended to be `debbiephoenix1985-cloud/Chaos-Emerging-Viewer`. Once established, a qualified release must be built directly from a protected release tag using a reviewed GitHub Actions workflow. Artifacts built locally or copied from earlier installers are not eligible to be submitted as verifiable builds from this repository.

Signing requests must be manually approved and the resulting files independently verified using Authenticode. Installed program files, installer metadata, and optional dependencies will be included in the review.

## SignPath eligibility limitation

The existing Chaos Emerging installer contains proprietary Vivox binaries. The repository does not publish those binaries and cannot use free SignPath Foundation services to sign the current packaged distribution as-is. The eligibility of any future fully open-source package must be confirmed by SignPath Foundation.

## Roles and change control

Project maintainer, code change approver and proposed release signing approver: [debbiephoenix1985-cloud](https://github.com/debbiephoenix1985-cloud). Additional reviewer/committer roles are not yet assigned. Repository maintainers must enable multifactor authentication, review contributions, and protect the release pipeline before requesting signing.

## Privacy and safety

The program connects to OpenSim grid servers, and may connect to external media, stream and voice endpoints selected through its use. Any future signed release must accurately document its network behavior and include uninstall support. Privacy behavior and third-party terms must be reviewed before an application is submitted.
