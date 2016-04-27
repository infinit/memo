$(document).ready(function() {

  function launchTerminal() {
    $('body.home #commands').t(
      '<ins>1 </ins><mark>$></mark> infinit-storage --create --s3 --name s3 --capacity 10TB' +
      '<ins>0.5</ins><ins>\nCreated storage "s3". \n\n</ins>' +
      '<mark>$></mark> <ins>2</ins>'+

      'infinit-network --create --storage s3 --storage local-disk-1 --storage local&#8209;disk&#8209;2 --replication-factor 3 --name hybrid-cloud' +
      '<ins>0.5</ins><ins>\nCreated network "hybrid-cloud". \n\n</ins>' +
      '<mark>$></mark> <ins>2</ins>'+

      'infinit-volume --create --network hybrid-cloud --name company' +
      '<ins>0.5</ins><ins>\nStarting network "hybrid-cloud".'+
      '\nCreating volume root blocks.'+
      '\nCreated volume "company". \n\n</ins>' +
      '<mark>$></mark> <ins>2</ins>'+

      'infinit-volume --mount --name company --mountpoint /mnt/company/' +
      '<ins>0.5</ins><ins>\nMounting volume "company". \n\n</ins>' +
      '<mark>$></mark> <ins>2</ins>'+

      'cp -R Engineering/ /mnt/company/'+
      '<ins>0.5</ins><ins>\n\n' +

      '<mark>$></mark> ',
      {
        speed: 20,
        speed_vary: true
      }
    );
  }

  if ($('body').hasClass('home')) {
    var has_reach_terminal = false;

    $(window).scroll(function () {
      if ($(window).scrollTop() > 700 && !has_reach_terminal) {
        has_reach_terminal = true;
        launchTerminal();
      }

      if ($(window).scrollTop() > 400) {
        $('.btn-contact').addClass('show');
      }
    });

    if (window.location.hash === '#slack') {
      $.magnificPopup.open({
        items: { src: '#slack'},
        type: 'inline'
      }, 0);

      $('#slack').show();
    }

    var client = algoliasearch("2BTFITEL0N", "f27772a74737b4bb66eb0999104830d2");
    var index = client.initIndex('infinit_sh_faq');
    autocomplete('#search', { hint: false }, [
      {
        source: autocomplete.sources.hits(index, { hitsPerPage: 5 }),
        displayKey: 'question',
        templates: {
          suggestion: function(suggestion) {
            return suggestion._highlightResult.question.value;
          }
        }
      }
    ]).on('autocomplete:selected', function(event, suggestion, dataset) {
      var query = $('#search').val();
      window.location.href = "/faq?q=" + query;
    });
  }

});
