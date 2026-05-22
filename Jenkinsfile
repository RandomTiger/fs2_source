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

        stage('Build Release') {
            steps {
                bat """
                    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe" ^
                    freespace2.sln ^
                    /t:Freespace2 ^
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
                    /t:Freespace2 ^
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
