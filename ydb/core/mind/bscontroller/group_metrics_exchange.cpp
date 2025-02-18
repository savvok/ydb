#include "impl.h"

namespace NKikimr::NBsController {

    class TBlobStorageController::TTxGroupMetricsExchange : public TTransactionBase<TBlobStorageController> {
        std::unique_ptr<TEventHandle<TEvBlobStorage::TEvControllerGroupMetricsExchange>> Ev;
        std::unique_ptr<TEvBlobStorage::TEvControllerGroupMetricsExchange> Response;

    public:
        TTxGroupMetricsExchange(TBlobStorageController *controller, TEvBlobStorage::TEvControllerGroupMetricsExchange::TPtr ev)
            : TBase(controller)
            , Ev(ev.Release())
        {}

        TTxType GetTxType() const override { return NBlobStorageController::TXTYPE_GROUP_METRICS_EXCHANGE; }

        bool Execute(TTransactionContext& txc, const TActorContext&) override {
            auto& record = Ev->Get()->Record;

            STLOG(PRI_DEBUG, BS_CONTROLLER, BSCTXGME00, "TTxGroupMetricsExchange::Execute", (Record, record));

            NIceDb::TNiceDb db(txc.DB);

            for (NKikimrBlobStorage::TGroupMetrics& item : *record.MutableGroupMetrics()) {
                if (TGroupInfo *group = Self->FindGroup(item.GetGroupId())) {
                    group->GroupMetrics = std::move(item);

                    TString s;
                    const bool success = group->GroupMetrics->SerializeToString(&s);
                    Y_VERIFY_DEBUG(success);
                    db.Table<Schema::Group>().Key(group->ID).Update<Schema::Group::Metrics>(s);
                }
            }

            if (record.GroupsToQuerySize()) {
                Response.reset(new TEvBlobStorage::TEvControllerGroupMetricsExchange);
                auto& outRecord = Response->Record;
                for (const TGroupId groupId : record.GetGroupsToQuery()) {
                    if (TGroupInfo *group = Self->FindGroup(groupId)) {
                        auto *item = outRecord.AddGroupMetrics();
                        item->SetGroupId(group->ID);
                        group->FillInGroupParameters(item->MutableGroupParameters());
                    }
                }
            }

            for (auto it = Self->SelectGroupsQueue.begin(); it != Self->SelectGroupsQueue.end(); ) {
                Self->ProcessSelectGroupsQueueItem(it++);
            }

            return true;
        }

        void Complete(const TActorContext&) override {
            if (Response) {
                Self->Send(Ev->Sender, Response.release(), 0, Ev->Cookie);
            }
        }
    };

    void TBlobStorageController::Handle(TEvBlobStorage::TEvControllerGroupMetricsExchange::TPtr& ev) {
        Execute(new TTxGroupMetricsExchange(this, ev));
    }

} // NKikimr::NBsController
