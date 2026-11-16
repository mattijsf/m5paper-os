package nl.paperos.companion

import android.content.Context
import androidx.work.BackoffPolicy
import androidx.work.Constraints
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.Worker
import androidx.work.WorkerParameters
import java.util.concurrent.TimeUnit

/** Drains the [Outbox] to the [Relay] when the network is up; exponential backoff on failure. */
class UploadWorker(ctx: Context, params: WorkerParameters) : Worker(ctx, params) {
    override fun doWork(): Result {
        val report = Drain.run(Outbox.of(applicationContext), Relay.current(applicationContext)) { isStopped }
        return when (report.result) {
            Drain.Result.EMPTY, Drain.Result.NOT_CONFIGURED -> Result.success()
            Drain.Result.RETRY_LATER, Drain.Result.STOPPED -> Result.retry()
        }
    }

    companion object {
        private const val NAME = "paperos-upload"

        /** Call after adding to the outbox. One drain at a time; a running drain picks up new items. */
        fun schedule(ctx: Context) {
            val req = OneTimeWorkRequestBuilder<UploadWorker>()
                .setConstraints(Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build())
                .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, 30, TimeUnit.SECONDS)
                .build()
            WorkManager.getInstance(ctx).enqueueUniqueWork(NAME, ExistingWorkPolicy.KEEP, req)
        }
    }
}
