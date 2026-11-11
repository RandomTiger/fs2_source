pipeline {
    agent any

    triggers {
        pollSCM('H/15 * * * *')
    }

    stages {

        stage('Clean') {
            steps {
                deleteDir()
            }
        }

        stage('Checkout') {
            steps {
                checkout scm
                echo "Checked out ${env.GIT_BRANCH} @ ${env.GIT_COMMIT}"
            }
        }

        stage('Check PATH_FS2') {
            steps {
                // Claude: every project links its exe into %PATH_FS2%; stop here if it is unset (D-20261107-104240)
                bat 'if "%PATH_FS2%"=="" (echo PATH_FS2 is not set & exit /b 1)'
            }
        }

        stage('Toolset diagnostics') {
            steps {
                // Claude: temporary, for the MFC error MSB8041 on HelpEd: shows the VC environment the job sees and
                // the MFC library MSBuild checks for. Never fails the build; remove once the error is understood.
                bat """
                    set VC
                    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe" ^
                    HelpEd.vcxproj /t:PrepareForBuild /p:Configuration=Release /p:Platform=Win32 /v:diag /nologo ^
                    | findstr /i /c:"VCToolsInstallDir =" /c:"VCToolsVersion =" /c:"MFC_KeyFile =" /c:"PlatformShortName ="
                    exit /b 0
                """
            }
        }

        stage('Build Release') {
            steps {
                bat """
                    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe" ^
                    freespace2.sln ^
                    /p:Configuration=Release ^
                    /p:Platform=x86 ^
                    /m
                """
            }
        }

        stage('Build Debug') {
            steps {
                bat """
                    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe" ^
                    freespace2.sln ^
                    /p:Configuration=Debug ^
                    /p:Platform=x86 ^
                    /m
                """
            }
        }

    }

    post {
        always {
            echo "Pipeline finished with status: ${currentBuild.currentResult}"
        }
    }
}
